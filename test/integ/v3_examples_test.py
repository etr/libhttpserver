#!/usr/bin/env python3
"""Bounded public example smoke tests; H2/H3 checks are configuration only."""
import contextlib
import hashlib
import base64
import http.client
import os
from pathlib import Path
import queue
import socket
import subprocess
import threading
import unittest


EXAMPLES = Path(os.environ.get('V3_EXAMPLES_DIR', '../examples')).resolve()


@contextlib.contextmanager
def running(name):
    binary = EXAMPLES / name
    assert binary.is_file(), f'missing compiled example: {binary}'
    with subprocess.Popen([str(binary), '10'], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE) as child:
        startup = queue.Queue(maxsize=1)
        reader = threading.Thread(target=lambda: startup.put(child.stdout.readline(128)), daemon=True)
        reader.start()
        try:
            try:
                line = startup.get(timeout=5)
            except queue.Empty:
                raise AssertionError(f'{name}: startup deadline expired') from None
            assert line.startswith(b'PORT='), f'{name}: invalid startup: {line!r}'
            yield child, int(line[5:])
        finally:
            if child.poll() is None:
                child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=3)
            reader.join(timeout=1)
            child.communicate(timeout=3)


def request(port, method, path, body=None):
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
    try:
        connection.request(method, path, body=body)
        response = connection.getresponse()
        return response.status, response.getheader('Content-Length'), response.read()
    finally:
        connection.close()


def exact(sock, count):
    result = bytearray()
    while len(result) < count:
        part = sock.recv(count - len(result))
        assert part, 'WebSocket closed before expected frame'
        result.extend(part)
    return bytes(result)


def send_frame(sock, opcode, payload):
    assert len(payload) < 126
    mask = b'\x01\x02\x03\x04'
    sock.sendall(bytes([0x80 | opcode, 0x80 | len(payload)]) + mask
                 + bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload)))


def receive_frame(sock):
    head = exact(sock, 2)
    assert head[0] & 0x80 and not head[1] & 0x80, head
    length = head[1] & 0x7f
    if length == 126:
        length = int.from_bytes(exact(sock, 2), 'big')
    elif length == 127:
        length = int.from_bytes(exact(sock, 8), 'big')
    assert length <= 65536, 'unexpected unbounded frame'
    return head[0] & 0x0f, exact(sock, length)


class ExampleTests(unittest.TestCase):
    def test_routes_sync_coroutine_head_and_body_cap(self):
        with running('v3_routes') as (_, port):
            self.assertEqual(request(port, 'GET', '/hello'),
                             (200, '14', b'hello from v3\n'))
            self.assertEqual(request(port, 'HEAD', '/hello'), (200, '14', b''))
            self.assertEqual(request(port, 'POST', '/echo', b'abc\x00def'),
                             (200, '7', b'abc\x00def'))
            self.assertEqual(request(port, 'POST', '/echo', b'x' * 1024),
                             (200, '1024', b'x' * 1024))
            self.assertEqual(request(port, 'POST', '/echo', b'x' * 1025)[0], 413)

    def test_handler_stop_exits_without_deadlock(self):
        with running('v3_routes') as (child, port):
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'POST /stop HTTP/1.1\r\nHost: localhost\r\n'
                             b'Content-Length: 0\r\n\r\n')
                # request_stop cancels active exchanges; a reply is not promised.
                self.assertEqual(child.wait(timeout=3), 0)

    def test_protocol_selection_reports_public_boundary(self):
        binary = EXAMPLES / 'v3_protocol_options'
        self.assertTrue(binary.is_file(), f'missing compiled example: {binary}')
        result = subprocess.run([str(binary)], capture_output=True, timeout=5,
                                check=True, text=True)
        self.assertIn('HTTP/1.1 validate=ok listen=ok', result.stdout)
        for version in ('HTTP/2', 'HTTP/3'):
            self.assertRegex(result.stdout, version + r' validate=(ok|not_supported) listen=not_supported port=0')
        self.assertIn('TLS validate=', result.stdout)
        self.assertIn('listen=not_supported port=0', result.stdout)

    def test_websocket_rejection_echo_and_close(self):
        with running('v3_websocket_echo') as (_, port):
            self.assertEqual(request(port, 'GET', '/echo')[0], 400)
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                key = 'dGhlIHNhbXBsZSBub25jZQ=='
                sock.sendall(('GET /echo HTTP/1.1\r\nHost: localhost\r\n'
                              'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                              f'Sec-WebSocket-Key: {key}\r\n'
                              'Sec-WebSocket-Version: 13\r\n\r\n').encode())
                header = bytearray()
                while not header.endswith(b'\r\n\r\n'):
                    header.extend(exact(sock, 1))
                    self.assertLess(len(header), 8192)
                self.assertTrue(header.startswith(b'HTTP/1.1 101 '), header)
                accept = base64.b64encode(hashlib.sha1(
                    (key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
                self.assertIn(accept, header)
                for opcode, payload in ((1, b'hello'), (2, b'\x00\xff')):
                    send_frame(sock, opcode, payload)
                    self.assertEqual(receive_frame(sock), (opcode, payload))
                send_frame(sock, 8, b'\x03\xe8')
                self.assertEqual(receive_frame(sock), (8, b'\x03\xe8'))

    def test_external_loop_public_example(self):
        with running('v3_external_event_loop') as (_, port):
            self.assertEqual(request(port, 'GET', '/hello'),
                             (200, '28', b'hello from an external loop\n'))

    @unittest.skipUnless(os.environ.get('V3_TLS_MODE') == 'yes', 'TLS-off build')
    def test_private_rotation_demonstration(self):
        binary = Path('v3_tls_rotation').resolve()
        self.assertTrue(binary.is_file(), f'missing private demonstration: {binary}')
        result = subprocess.run([str(binary)], capture_output=True, timeout=5,
                                check=True, text=True)
        self.assertEqual(result.stdout, 'private rotation: old=1 active=2 failed replacement preserved=2\n')


if __name__ == '__main__':
    unittest.main()
