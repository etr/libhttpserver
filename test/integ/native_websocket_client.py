#!/usr/bin/env python3
"""Independent RFC 6455 acceptance client. Full lane requires websockets==15.0.1."""
import argparse
import base64
import hashlib
import os
import queue
import socket
import struct
import subprocess
import threading
import time


def start(binary):
    process = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, bufsize=1)
    events = queue.Queue()
    threading.Thread(target=lambda: [events.put(line.strip()) for line in process.stdout], daemon=True).start()
    process.task122_errors = []

    def drain_errors():
        remaining = 65536
        for line in process.stderr:
            if remaining > 0:
                process.task122_errors.append(line[:remaining])
                remaining -= len(line)

    process.task122_error_reader = threading.Thread(target=drain_errors, daemon=True)
    process.task122_error_reader.start()
    try:
        ready = events.get(timeout=10)
        assert ready.startswith('READY '), ready
        return process, events, int(ready.split()[1])
    except BaseException:
        process.kill()
        process.wait(timeout=5)
        raise


def wait_event(events, prefix, deadline=10):
    until = time.monotonic() + deadline
    while time.monotonic() < until:
        line = events.get(timeout=max(0.01, until - time.monotonic()))
        if line.startswith(prefix):
            return line
    raise AssertionError('missing event: ' + prefix)


def stop(process):
    try:
        if process.poll() is None:
            try:
                process.stdin.write('STOP\n')
                process.stdin.flush()
            except BrokenPipeError:
                pass
        process.stdin.close()
        process.wait(timeout=10)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        process.task122_error_reader.join(timeout=1)
    assert process.returncode == 0, (process.returncode, ''.join(process.task122_errors))


def external(port):
    import websockets
    from websockets.sync.client import connect
    print('Independent client websockets', websockets.__version__, flush=True)
    with connect(f'ws://127.0.0.1:{port}/echo', origin='https://example.com',
                 subprotocols=['binary', 'chat'], open_timeout=5, close_timeout=5, proxy=None) as ws:
        assert ws.subprotocol == 'chat'
        assert 'Sec-WebSocket-Extensions' not in ws.response.headers
        assert 'Content-Length' not in ws.response.headers
        assert 'Transfer-Encoding' not in ws.response.headers
        ws.send('hello independent client')
        assert ws.recv(timeout=5) == 'hello independent client'
        ws.send(b'\x00\xff\x80')
        assert ws.recv(timeout=5) == b'\x00\xff\x80'
        assert ws.ping(b'ping').wait(5)
    assert ws.close_code == 1000
    print('Independent handshake/text/binary/ping/clean-close/extensions PASS', flush=True)


def external_backpressure(port, events):
    from websockets.sync.client import connect
    while not events.empty():
        events.get_nowait()
    with connect(f'ws://127.0.0.1:{port}/flood', max_queue=1, max_size=2097152,
                 open_timeout=5, close_timeout=5, proxy=None) as ws:
        ws.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        stable = wait_event(events, 'BLOCKED ').split()[1]
        until = time.monotonic() + 0.25
        while time.monotonic() < until:
            try:
                line = events.get(timeout=0.05)
                if line.startswith('BLOCKED '):
                    stable = line.split()[1]
                if stable and line == 'RESUMED ' + stable:
                    stable = None
            except queue.Empty:
                if stable:
                    break
        assert stable, 'external-client socket saturation unproven'
        ws.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1048576)
        for i in range(64):
            assert ws.recv(timeout=10) == bytes([i]) * 1048576, i
        wait_event(events, 'SENT 64')
    assert ws.close_code == 1000
    print('Independent websockets stalled-reader/backpressured-send/ordered resume PASS', flush=True)


def request(path='/echo', **replace):
    fields = {'Host': 'localhost', 'Upgrade': 'websocket', 'Connection': 'Upgrade',
              'Sec-WebSocket-Key': 'dGhlIHNhbXBsZSBub25jZQ==', 'Sec-WebSocket-Version': '13'}
    fields.update(replace)
    return ('GET ' + path + ' HTTP/1.1\r\n' + ''.join(k + ': ' + v + '\r\n' for k, v in fields.items() if v is not None) + '\r\n').encode()


def frame(opcode, payload=b''):
    mask = b'\x13\x37\x42\x69'
    size = len(payload)
    head = bytes([0x80 | opcode, 0x80 | (size if size < 126 else 126 if size <= 65535 else 127)])
    if size >= 126:
        head += struct.pack('!H' if size <= 65535 else '!Q', size)
    return head + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(payload))


class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=5)
        self.socket.settimeout(5)
        self.pending = b''

    def exact(self, size):
        while len(self.pending) < size:
            part = self.socket.recv(max(4096, size - len(self.pending)))
            assert part, 'unexpected EOF'
            self.pending += part
        data, self.pending = self.pending[:size], self.pending[size:]
        return data

    def head(self):
        while b'\r\n\r\n' not in self.pending:
            part = self.socket.recv(4096)
            assert part, 'EOF before handshake'
            self.pending += part
        head, self.pending = self.pending.split(b'\r\n\r\n', 1)
        return head

    def receive(self):
        a, b = self.exact(2)
        assert a & 0x70 == 0 and b & 0x80 == 0, 'extensions/masking from server'
        length = b & 127
        if length == 126:
            length = struct.unpack('!H', self.exact(2))[0]
        elif length == 127:
            length = struct.unpack('!Q', self.exact(8))[0]
        return a & 15, self.exact(length)

    def close(self):
        self.socket.close()


def raw_cases(port, events, process):
    peer = Peer(port)
    peer.socket.sendall(request('/idle'))
    assert peer.head().startswith(b'HTTP/1.1 101 ')
    wait_event(events, 'IDLE')
    # The short HTTP header deadline must no longer govern idle frames.
    time.sleep(0.2)
    process.stdin.write('SEND\n')
    process.stdin.flush()
    assert peer.receive() == (1, b'unsolicited')
    peer.socket.sendall(frame(8, b'\x03\xe8'))
    assert peer.receive() == (8, b'\x03\xe8')
    peer.close()
    print('Parked writer/application send wake/HTTP timer removal PASS', flush=True)
    malformed = [({'Sec-WebSocket-Key': 'YQ=='}, 400), ({'Sec-WebSocket-Version': '12'}, 426),
                 ({'Origin': 'https://evil.example.com'}, 403), ({'Upgrade': 'websocket,'}, 400),
                 ({'Connection': 'xupgrade'}, 400), ({'Sec-WebSocket-Key': None}, 400),
                 ({'Sec-WebSocket-Protocol': 'chat,chat'}, 400), ({'Content-Length': '1'}, 400)]
    for replacement, status in malformed:
        peer = Peer(port)
        peer.socket.sendall(request(**replacement))
        head = peer.head()
        assert head.startswith(f'HTTP/1.1 {status} '.encode()), head
        if status == 426:
            assert b'Sec-WebSocket-Version: 13' in head
        peer.close()
    # Every boundary of the handshake; masked first frame in the final read.
    head = request()
    for split in range(len(head) + 1):
        peer = Peer(port)
        peer.socket.sendall(head[:split])
        peer.socket.sendall(head[split:] + frame(1, b'first') + frame(2, b'second') + frame(1, b'third'))
        response = peer.head()
        assert response.startswith(b'HTTP/1.1 101 '), response
        accept = base64.b64encode(hashlib.sha1(b'dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
        assert b'Sec-WebSocket-Accept: ' + accept in response
        assert b'Connection: Upgrade' in response
        assert b'Content-Length' not in response and b'Transfer-Encoding' not in response
        assert peer.receive() == (1, b'first')
        assert peer.receive() == (2, b'second')
        assert peer.receive() == (1, b'third')
        peer.socket.sendall(frame(8, b'\x03\xe8'))
        assert peer.receive() == (8, b'\x03\xe8')
        peer.close()
    print('Raw refusals/all handshake splits/coalesced frames/queue-full suffix PASS', flush=True)
    peer = Peer(port)
    peer.socket.sendall(request() + b'GET / HTTP/1.1\r\n\r\n')
    assert peer.head().startswith(b'HTTP/1.1 101 ')
    assert peer.socket.recv(1) == b'', 'HTTP-looking bytes must be invalid frames after transfer'
    peer.close()
    for path in ('/return', '/throw'):
        peer = Peer(port)
        peer.socket.sendall(request(path))
        assert peer.head().startswith(b'HTTP/1.1 101 ')
        assert peer.socket.recv(1) == b''
        peer.close()
    print('HTTP-looking frame rejection/handler return/handler throw PASS', flush=True)


def backpressure(port, events):
    # Empty prior fixture events so this wait concerns only the producer.
    while not events.empty():
        events.get_nowait()
    peer = Peer(port)
    peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    peer.socket.sendall(request('/flood'))
    assert peer.head().startswith(b'HTTP/1.1 101 ')
    first_block = wait_event(events, 'BLOCKED ')
    # Observe a writable operation that remains unresumed with reads paused.
    until = time.monotonic() + 0.25
    stable = first_block.split()[1]
    while time.monotonic() < until:
        try:
            line = events.get(timeout=0.05)
            if line.startswith('BLOCKED '):
                stable = line.split()[1]
            if stable and line == 'RESUMED ' + stable:
                stable = None
        except queue.Empty:
            if stable:
                break
    assert stable, 'actual socket saturation / parked writable not observed'
    peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1048576)
    for i in range(64):
        opcode, payload = peer.receive()
        assert opcode == 2 and len(payload) == 1048576
        assert payload == bytes([i]) * 1048576, i
    wait_event(events, 'SENT 64')
    peer.socket.sendall(frame(8, b'\x03\xe8'))
    assert peer.receive() == (8, b'\x03\xe8')
    peer.close()
    print('Real socket saturation/parked writable/64 ordered MiB/retry PASS', flush=True)
    peer = Peer(port)
    peer.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    peer.socket.sendall(request('/flood'))
    assert peer.head().startswith(b'HTTP/1.1 101 ')
    wait_event(events, 'BLOCKED ')
    peer.close()
    wait_event(events, 'WRITABLE CLOSED')
    print('Abort while writable blocked PASS', flush=True)


def isolation_case(port):
    one, two, failing = Peer(port), Peer(port), Peer(port)
    try:
        for peer, path in [(one, '/echo'), (two, '/echo'), (failing, '/app-close')]:
            peer.socket.sendall(request(path))
            assert peer.head().startswith(b'HTTP/1.1 101 ')
        assert failing.receive() == (8, b'\x03\xe8local')
        assert failing.socket.recv(1) == b''
        for peer, payload in [(one, b'first alive'), (two, b'second alive')]:
            peer.socket.sendall(frame(1, payload))
            assert peer.receive() == (1, payload)
        http = Peer(port)
        try:
            http.socket.sendall(b'GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n')
            assert http.head().startswith(b'HTTP/1.1 200 ')
            assert http.exact(4) == b'okay'
        finally:
            http.close()
        print('Local Close timeout preserves two WebSockets and HTTP PASS', flush=True)
    finally:
        one.close()
        two.close()
        failing.close()


def handler_drain_case(binary):
    process, events, port = start(binary)
    peer = Peer(port)
    try:
        peer.socket.sendall(request('/drain-handler'))
        assert peer.head().startswith(b'HTTP/1.1 101 ')
        assert wait_event(events, 'DRAIN HANDLER') == 'DRAIN HANDLER would_deadlock'
        assert peer.receive() == (8, b'\x03\xe9server drain')
        peer.socket.sendall(frame(8, b'\x03\xe9server drain'))
        wait_event(events, 'CLOSE 1 ')
        assert peer.socket.recv(1) == b''
        print('Handler-safe drain/would_deadlock/clean Close PASS', flush=True)
    finally:
        peer.close()
        stop(process)


def drain_case(binary, mode='DRAIN', independent=False):
    process, events, port = start(binary)
    peer = None
    http = None
    try:
        if independent:
            from websockets.sync.client import connect
            peer = connect(f'ws://127.0.0.1:{port}/echo', open_timeout=5,
                           close_timeout=5, proxy=None)
        else:
            peer = Peer(port)
            peer.socket.sendall(request())
            assert peer.head().startswith(b'HTTP/1.1 101 ')
        wait_event(events, 'ACCEPT')
        http = Peer(port)
        http.socket.sendall(b'GET /slow HTTP/1.1\r\nHost: localhost\r\n\r\n')
        assert http.head().startswith(b'HTTP/1.1 200 ')
        assert http.exact(3) == b'old'
        started = time.monotonic()
        process.stdin.write(mode + '\n')
        process.stdin.flush()
        wait_event(events, 'DRAIN START', deadline=2)
        if independent:
            from websockets.exceptions import ConnectionClosedOK
            try:
                peer.recv(timeout=5)
                raise AssertionError('expected server Close')
            except ConnectionClosedOK:
                assert peer.close_code == 1001 and peer.close_reason == 'server drain'
        else:
            assert peer.receive() == (8, b'\x03\xe9server drain')
            if mode == 'DRAIN':
                peer.socket.sendall(frame(8, b'\x03\xe9server drain'))
            else:
                peer.socket.settimeout(0.05)
                try:
                    assert peer.socket.recv(1) != b'', 'cancelled before deadline'
                except socket.timeout:
                    pass
                peer.socket.settimeout(5)
                assert peer.socket.recv(1) == b'', 'transport stayed live after deadline'
                assert 0.10 <= time.monotonic() - started < 3
        assert http.exact(4) == b'body', 'drain truncated in-flight HTTP'
        if mode != 'DRAIN-DROP':
            status = wait_event(events, 'DRAIN RESULT ')
            assert status.split()[2] == ('completed' if mode == 'DRAIN' else 'expired'), status
        print('Drain ' + mode + (' external' if independent else ' raw') + ' PASS', flush=True)
    finally:
        if http is not None:
            http.close()
        if peer is not None:
            peer.close()
        stop(process)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('fixture')
    parser.add_argument('--independent', action='store_true')
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--drain-only', action='store_true')
    args = parser.parse_args()
    if args.drain_only:
        drain_case(args.fixture)
        return
    process, events, port = start(args.fixture)
    try:
        if args.independent:
            external(port)
            if not args.smoke:
                external_backpressure(port, events)
        if not args.smoke:
            raw_cases(port, events, process)
            backpressure(port, events)
            isolation_case(port)
    finally:
        stop(process)
    handler_drain_case(args.fixture)
    drain_case(args.fixture)
    drain_case(args.fixture, 'DRAIN-LATE')
    drain_case(args.fixture, 'DRAIN-DROP')
    if args.independent:
        drain_case(args.fixture, independent=True)


if __name__ == '__main__':
    main()
