#!/usr/bin/env python3
"""Bounded public example smoke tests against native installed capabilities."""
import contextlib
import asyncio
import importlib.util
import hashlib
import base64
import http.client
import os
from pathlib import Path
import queue
import socket
import select
import ssl
import subprocess
import threading
import time
import unittest
import tempfile
import urllib.request
import urllib.error
from concurrent.futures import ThreadPoolExecutor


EXAMPLES = Path(os.environ.get('V3_EXAMPLES_DIR', '../examples')).resolve()


def retire_child(child, name, stderr, successful):
    try:
        if successful:
            child.wait(timeout=15)
    finally:
        if child.poll() is None:
            child.terminate()
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=3)
        stderr.seek(0)
        child.example_stderr = stderr.read().decode(errors='replace')
    if successful:
        assert child.returncode == 0, f'{name}: exit {child.returncode}\n{child.example_stderr}'
        diagnostics = ('Sanitizer', 'runtime error:')
        assert not any(marker in child.example_stderr for marker in diagnostics), (
            f'{name}: sanitizer diagnostics\n{child.example_stderr}')


@contextlib.contextmanager
def running(name, *arguments):
    binary = EXAMPLES / name
    assert binary.is_file(), f'missing compiled example: {binary}'
    with tempfile.TemporaryFile() as stderr, subprocess.Popen(
            [str(binary), *(arguments or ('3',))], stdout=subprocess.PIPE, stderr=stderr) as child:
        startup = queue.Queue(maxsize=1)
        reader = threading.Thread(target=lambda: startup.put(child.stdout.readline(128)), daemon=True)
        reader.start()
        successful = False
        try:
            try:
                line = startup.get(timeout=5)
            except queue.Empty:
                raise AssertionError(f'{name}: startup deadline expired') from None
            assert line.startswith(b'PORT='), f'{name}: invalid startup: {line!r}'
            yield child, int(line[5:])
            successful = True
        finally:
            try:
                retire_child(child, name, stderr, successful)
            finally:
                reader.join(timeout=1)


@contextlib.contextmanager
def controlled(name, *arguments, numeric=False):
    binary = EXAMPLES / name
    assert binary.is_file(), f'missing compiled example: {binary}'
    with tempfile.TemporaryFile() as stderr, subprocess.Popen(
            [str(binary), *arguments], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr) as child:
        lines = queue.Queue()
        def read_lines():
            for line in child.stdout: lines.put(line.strip())
        reader = threading.Thread(target=read_lines, daemon=True); reader.start()
        def line():
            try: return lines.get(timeout=5)
            except queue.Empty: raise AssertionError(f'{name}: output deadline expired') from None
        def command(text):
            child.stdin.write(text.encode() + b'\n'); child.stdin.flush()
        successful = False
        try:
            first = line()
            assert numeric or first.startswith(b'PORT='), first
            yield child, int(first if numeric else first[5:]), command, line
            successful = True
        finally:
            try:
                retire_child(child, name, stderr, successful)
            finally:
                reader.join(timeout=1)


def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
    try:
        connection.request(method, path, body=body, headers=headers or {})
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


class HarnessTests(unittest.TestCase):
    def test_helpers_reject_child_failure_after_startup(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'failed-host'
            binary.write_text('#!/usr/bin/env python3\nimport sys\nprint("PORT=1", flush=True)\n'
                              'print("ERROR: LeakSanitizer: detected memory leaks", file=sys.stderr)\nsys.exit(1)\n')
            binary.chmod(0o700)
            for helper in [running, controlled]:
                with self.subTest(helper=helper.__name__):
                    with self.assertRaisesRegex(AssertionError, 'LeakSanitizer'):
                        with helper(str(binary)) as result:
                            result[0].wait(timeout=3)

    def test_helpers_reject_sanitizer_diagnostics_even_on_zero_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'diagnostic-host'
            binary.write_text('#!/usr/bin/env python3\nimport sys\nprint("PORT=1", flush=True)\n'
                              'print("runtime error: invalid memory access", file=sys.stderr)\n')
            binary.chmod(0o700)
            for helper in [running, controlled]:
                with self.subTest(helper=helper.__name__):
                    with self.assertRaisesRegex(AssertionError, 'runtime error'):
                        with helper(str(binary)) as result:
                            result[0].wait(timeout=3)

    def test_running_waits_for_natural_retirement(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'retired'
            binary = Path(directory) / 'retiring-host'
            binary.write_text('#!/usr/bin/env python3\nimport pathlib, time\nprint("PORT=1", flush=True)\n'
                              f'time.sleep(.1)\npathlib.Path({str(marker)!r}).write_text("retired")\n')
            binary.chmod(0o700)
            with running(str(binary)):
                pass
            self.assertTrue(marker.exists(), 'host was terminated before natural retirement')


class ExampleTests(unittest.TestCase):
    def test_route_precedence_methods_errors_and_exception(self):
        with running('v3_routes') as (_, port):
            for path, body in [('/items/special', b'special\n'), ('/items/42', b'42\n'),
                               ('/tree/leaf', b'full\n'), ('/tree/deep/a', b'deep\n'),
                               ('/tree/other', b'prefix\n')]:
                self.assertEqual(request(port, 'GET', path)[2], body)
            self.assertEqual(request(port, 'CUSTOM', '/extension')[2], b'custom\n')
            self.assertEqual(request(port, 'custom', '/extension')[0], 200)
            self.assertEqual(request(port, 'GET', '/absent'), (404, '10', b'not found\n'))
            c = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
            try:
                c.request('DELETE', '/methods'); r = c.getresponse()
                self.assertEqual(r.status, 405)
                self.assertEqual(set(r.getheader('Allow').replace(' ', '').split(',')), {'GET', 'POST'})
                self.assertEqual(r.read(), b'method denied\n')
            finally:
                c.close()
            self.assertEqual(request(port, 'GET', '/throw')[0], 500)
            self.assertNotIn(b'private', request(port, 'GET', '/throw')[2])
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'GET /throw-committed HTTP/1.1\r\nHost: localhost\r\n\r\n')
                wire = bytearray()
                while part := sock.recv(8192): wire.extend(part)
                self.assertLessEqual(wire.count(b'HTTP/1.1'), 1)
                self.assertNotIn(b'500', wire)

    def test_request_values_preserve_wire_order(self):
        with running('v3_request_data') as (_, port):
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'GET /inspect?a=one&a=two+words&encoded=%2F HTTP/1.1\r\nHost: localhost\r\nX-Repeat: first\r\nX-Repeat: second\r\nCookie: a=1; a=2\r\nCookie: b=3\r\n\r\n')
                r = http.client.HTTPResponse(sock); r.begin(); body = r.read()
                self.assertEqual(r.status, 200)
                self.assertEqual(len(r.headers.get_all('Set-Cookie')), 2)
                self.assertIn(b'raw=/inspect?a=one&a=two+words&encoded=%2F\npath=/inspect\n', body)
                self.assertIn(b'header=first\nheader=second\n', body)
                self.assertIn(b'cookie=a=1\ncookie=a=2\ncookie=b=3\n', body)
                self.assertIn(b'a=one\na=two words\nencoded=/\n', body)
            self.assertEqual(request(port, 'GET', '/inspect?a=%')[0], 400)
            self.assertEqual(request(port, 'GET', '/inspect?a=' + 'x' * 257)[0], 413)

    def test_auth_guards(self):
        with running('v3_auth') as (_, port):
            base = f'http://127.0.0.1:{port}'
            for kind in ['basic', 'digest']:
                status, _, _ = request(port, 'GET', '/auth/' + kind)
                self.assertEqual(status, 401)
                for password, expected in [('secret', b'authenticated\n200'), ('wrong', b'401')]:
                    result = subprocess.run(['curl', '--silent', '--show-error', '--max-time', '3',
                        '--' + kind, '-u', 'demo:' + password, '-w', '%{http_code}', base + '/auth/' + kind],
                        capture_output=True, timeout=5, check=True)
                    self.assertEqual(result.stdout, expected)
            self.assertEqual(request(port, 'GET', '/public')[2], b'public\n')

    def test_form_read_reflection_has_plain_text_type(self):
        with tempfile.TemporaryDirectory() as directory:
            with running('v3_forms', directory, '3') as (_, port):
                c = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                try:
                    c.request('POST', '/form/read', '%3Chtml%3E%3Cscript%3Ealert(1)%3C%2Fscript%3E=x',
                              {'Content-Type': 'application/x-www-form-urlencoded'})
                    r = c.getresponse()
                    self.assertEqual(r.status, 200)
                    self.assertEqual(r.getheader('Content-Type'), 'text/plain; charset=utf-8')
                    self.assertEqual(r.read(), b'<html><script>alert(1)</script>=x\n')
                finally:
                    c.close()

    def test_retained_upload_survives_same_name_uploads(self):
        with tempfile.TemporaryDirectory() as directory:
            with running('v3_forms', directory, '3') as (_, port):
                body = b'--demo\r\nContent-Disposition: form-data; name="file"; filename="safe.txt"\r\n\r\nabc'
                headers = {'Content-Type': 'multipart/form-data; boundary=demo'}
                self.assertEqual(request(port, 'POST', '/multipart/keep', body + b'\r\n--demo--\r\n', headers)[0], 200)
                retained, = Path(directory).iterdir()
                self.assertEqual(retained.read_bytes(), b'abc')
                self.assertEqual(request(port, 'POST', '/multipart', body + b'\r\n--demo--\r\n', headers)[0], 200)
                self.assertTrue(retained.exists(), 'ordinary same-name upload removed retained data')
                self.assertEqual(request(port, 'POST', '/multipart', body, headers)[0], 400)
                self.assertEqual(retained.read_bytes(), b'abc')
                self.assertEqual(list(Path(directory).iterdir()), [retained])

    def test_forms_limits_files_and_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            with running('v3_forms', directory, '3') as (_, port):
                for path in ['/form', '/form/read']:
                    self.assertEqual(request(port, 'POST', path, 'a=one&a=two+words', {'Content-Type': 'application/x-www-form-urlencoded'})[2], b'a=one\na=two words\n')
                    self.assertEqual(request(port, 'POST', path, 'a=%', {'Content-Type': 'application/x-www-form-urlencoded'})[0], 400)
                    self.assertEqual(request(port, 'POST', path, 'a=' + 'x' * 1025, {'Content-Type': 'application/x-www-form-urlencoded'})[0], 413)
                def upload(path, close=True):
                    body = (b'--demo\r\nContent-Disposition: form-data; name="note"\r\n\r\none\r\n'
                            b'--demo\r\nContent-Disposition: form-data; name="note"\r\n\r\ntwo\r\n'
                            b'--demo\r\nContent-Disposition: form-data; name="file"; filename="safe.txt"\r\n\r\nabc')
                    if close: body += b'\r\n--demo--\r\n'
                    c = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                    try:
                        c.request('POST', path, body, {'Content-Type': 'multipart/form-data; boundary=demo'})
                        r = c.getresponse(); return r.status, r.read()
                    finally: c.close()
                self.assertEqual(upload('/multipart'), (200, b'note=one\nnote=two\nfile=safe.txt bytes=3\n'))
                self.assertEqual(list(Path(directory).iterdir()), [])
                self.assertEqual(upload('/multipart', False)[0], 400)
                self.assertEqual(list(Path(directory).iterdir()), [])
                for filename, payload, expected in [('large.txt', b'x' * 1025, 413), ('..', b'abc', 400), ('../sanitized.txt', b'abc', 200)]:
                    body = b'--demo\r\nContent-Disposition: form-data; name="file"; filename="' + filename.encode() + b'"\r\n\r\n' + payload + b'\r\n--demo--\r\n'
                    c = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                    try:
                        c.request('POST', '/multipart', body, {'Content-Type': 'multipart/form-data; boundary=demo'})
                        r = c.getresponse(); self.assertEqual(r.status, expected); r.read()
                    finally: c.close()
                    self.assertEqual(list(Path(directory).iterdir()), [])
                with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                    sock.sendall(b'POST /multipart HTTP/1.1\r\nHost: localhost\r\nContent-Type: multipart/form-data; boundary=demo\r\nTransfer-Encoding: chunked\r\nExpect: 100-continue\r\n\r\n')
                    interim = bytearray()
                    while not interim.endswith(b'\r\n\r\n'): interim.extend(exact(sock, 1))
                    self.assertTrue(interim.startswith(b'HTTP/1.1 100'), interim)
                    partial = b'--demo\r\nContent-Disposition: form-data; name="file"; filename="partial.txt"\r\n\r\nabc'
                    sock.sendall(f'{len(partial):x}\r\n'.encode() + partial + b'\r\n')
                    deadline = time.monotonic() + 3
                    while not list(Path(directory).iterdir()) and time.monotonic() < deadline: threading.Event().wait(.005)
                    partials = list(Path(directory).iterdir())
                    self.assertEqual(len(partials), 1, 'multipart sink never began the partial file')
                    path = partials[0]
                deadline = time.monotonic() + 3
                while path.exists() and time.monotonic() < deadline: threading.Event().wait(.005)
                self.assertFalse(path.exists(), 'disconnected upload did not clean its partial file')
                self.assertEqual(upload('/multipart/keep')[0], 200)
                retained, = Path(directory).iterdir()
                self.assertNotEqual(retained.name, 'safe.txt')
                self.assertEqual(retained.read_bytes(), b'abc')

    def test_streaming_admission_trailers_and_cap(self):
        with running('v3_body_streaming') as (_, port):
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'POST /reject HTTP/1.1\r\nHost: localhost\r\nExpect: 100-continue\r\nContent-Length: 10000\r\n\r\n')
                self.assertTrue(sock.recv(8192).startswith(b'HTTP/1.1 403'))
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'POST /upload HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\nX-End: first\r\nX-End: second\r\n\r\n')
                r = http.client.HTTPResponse(sock); r.begin()
                self.assertEqual(r.read(), b'bytes=5\ntrailer=first\ntrailer=second\n')
            self.assertEqual(request(port, 'POST', '/upload', b'x' * 1024)[0], 200)
            self.assertEqual(request(port, 'POST', '/upload', b'x' * 1025)[0], 413)
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'GET /download HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                wire = bytearray()
                while part := sock.recv(8192): wire.extend(part)
                self.assertIn(b'X-End: first\r\nX-End: second\r\n', wire)
                self.assertEqual(wire.count(b'generated-chunk\n'), 64)

    def test_download_resumes_after_slow_reader(self):
        with controlled('v3_body_streaming', '10') as (_, port, command, line):
            with socket.socket() as sock:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
                sock.settimeout(5); sock.connect(('127.0.0.1', port))
                sock.sendall(b'GET /download/slow HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                header = bytearray()
                while not header.endswith(b'\r\n\r\n'): header.extend(exact(sock, 1))
                self.assertTrue(header.startswith(b'HTTP/1.1 200'), header)
                self.assertEqual(line(), b'DOWNLOAD_STARTED')
                # A small receive window exercises resumed transport consumption.
                wire = bytearray()
                while part := sock.recv(65536): wire.extend(part)
                self.assertEqual(wire.count(b'x'), 128 * 32768)
                self.assertIn(b'X-End: slow-complete\r\n', wire)
                self.assertEqual(line(), b'DOWNLOAD_COMPLETE')

    def test_response_sources_reuse_and_one_shot(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'body.txt'; source.write_text('reopened\n')
            with running('v3_response_sources', str(source), '3') as (_, port):
                for kind, expected in [('bytes', b'owned\n'), ('borrowed', b'leased\n'), ('reopen', b'reopened\n'), ('factory', b'fresh\n')]:
                    def send_source(_):
                        c = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                        try:
                            c.request('GET', '/' + kind); r = c.getresponse()
                            return r.status, r.getheader('X-Send'), r.read()
                        finally: c.close()
                    with ThreadPoolExecutor(max_workers=4) as pool:
                        replies = list(pool.map(send_source, range(4)))
                    self.assertTrue(all(r[0] == 200 and r[2] == expected for r in replies))
                    self.assertEqual(len({r[1] for r in replies}), 4)
                for kind, expected in [('file', b'position\n'), ('pipe', b'pipe-eof\n')]:
                    self.assertEqual(request(port, 'GET', '/' + kind)[2], expected)
                    self.assertEqual(request(port, 'GET', '/' + kind)[0], 409)
                self.assertEqual(request(port, 'GET', '/closes')[2], b'file=1 pipe=1\n')
                self.assertEqual(request(port, 'GET', '/missing')[0], 404)
                with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                    sock.sendall(b'GET /icy HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                    self.assertTrue(sock.recv(8192).startswith(b'ICY 200'), 'expected the public ICY status flag on the wire')
                with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                    sock.sendall(b'GET /factory HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n')
                    wire = bytearray()
                    while part := sock.recv(8192): wire.extend(part)
                    self.assertIn(b'X-Send:', wire)
                    self.assertIn(b'X-End: definition\r\n', wire)

    def test_operations_hooks_policy_and_diagnostics(self):
        with controlled('v3_operations') as (child, port, command, line):
            self.assertEqual(request(port, 'GET', '/short')[0], 202)
            self.assertEqual(request(port, 'GET', '/throw')[0], 500)
            self.assertEqual(request(port, 'GET', '/absent')[0], 404)
            c = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
            try:
                c.request('GET', '/peer'); r = c.getresponse()
                self.assertEqual(r.getheader('X-Hook'), 'after')
                self.assertIn(b'127.0.0.1', r.read())
                command('deny-loopback'); self.assertEqual(line(), b'DENIED')
                c.request('GET', '/peer')
                with self.assertRaises((http.client.RemoteDisconnected, ConnectionResetError)):
                    c.getresponse()
            finally: c.close()
            command('allow-loopback'); self.assertEqual(line(), b'ALLOWED')
            self.assertEqual(request(port, 'GET', '/peer')[0], 200)
            deadline = time.monotonic() + 3
            while True:
                command('stats'); report = line().decode()
                counters = dict(item.split('=') for item in report.split())
                if (int(counters['sent']) > 0 and int(counters['completed']) > 0) or time.monotonic() >= deadline:
                    break
                threading.Event().wait(.005)
            self.assertGreater(int(counters['sent']), 0, report)
            self.assertGreater(int(counters['completed']), 0, report)
            self.assertIn('exceptions=1', report)
            self.assertIn('misses=1', report)
            self.assertRegex(report, r'delivered=[1-9]')
            deadline = time.monotonic() + 3
            while 'active=0' not in report and time.monotonic() < deadline:
                command('stats'); report = line().decode()
            self.assertIn('active=0', report)
            self.assertIn('streams=0', report)
            self.assertIn('queued=0', report)
            command('stop'); self.assertEqual(child.wait(timeout=3), 0)

    def test_operations_refuses_fifth_live_connection(self):
        with controlled('v3_operations') as (child, port, command, line):
            with contextlib.ExitStack() as stack:
                for _ in range(4):
                    sock = stack.enter_context(socket.create_connection(('127.0.0.1', port), timeout=3))
                    sock.sendall(b'GET /peer HTTP/1.1\r\nHost: localhost\r\n\r\n')
                    r = http.client.HTTPResponse(sock); r.begin(); self.assertEqual(r.status, 200); r.read()
                with socket.create_connection(('127.0.0.1', port), timeout=3) as refused:
                    try:
                        refused.sendall(b'GET /peer HTTP/1.1\r\nHost: localhost\r\n\r\n')
                        self.assertEqual(refused.recv(8192), b'')
                    except ConnectionResetError: pass
            deadline = time.monotonic() + 3
            while True:
                command('stats'); report = line().decode()
                if 'active=0' in report or time.monotonic() >= deadline: break
            self.assertIn('active=0', report)
            self.assertIn('streams=0', report)
            self.assertIn('queued=0', report)
            command('stop'); self.assertEqual(child.wait(timeout=3), 0)

    def test_lifecycle_resume_timeout_disconnect_and_stop(self):
        with controlled('v3_lifecycle') as (child, port, command, line):
            self.assertEqual(request(port, 'GET', '/timeout')[0], 408)
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'GET /wait HTTP/1.1\r\nHost: localhost\r\n\r\n')
                self.assertEqual(line(), b'PENDING')
                command('resume'); self.assertEqual(line(), b'SIGNALED')
                r = http.client.HTTPResponse(sock); r.begin(); self.assertEqual(r.read(), b'resumed\n')
                self.assertEqual(line(), b'RETIRED')
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'GET /cancel HTTP/1.1\r\nHost: localhost\r\n\r\n')
                self.assertEqual(line(), b'PENDING')
            self.assertEqual(line(), b'CANCELLED')
            self.assertEqual(line(), b'RETIRED')
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.sendall(b'POST /stop HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n')
                self.assertEqual(line(), b'STOP_REQUESTED')
            command('stop'); self.assertEqual(child.wait(timeout=3), 0)

    def test_lifecycle_drain_completion_and_deadline(self):
        for release in [True, False]:
            with controlled('v3_lifecycle') as (child, port, command, line):
                with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                    sock.sendall(b'GET /wait HTTP/1.1\r\nHost: localhost\r\n\r\n')
                    self.assertEqual(line(), b'PENDING')
                    command('drain'); self.assertEqual(line(), b'DRAINING')
                    if release:
                        command('resume'); self.assertEqual(line(), b'SIGNALED')
                        r = http.client.HTTPResponse(sock); r.begin(); self.assertEqual(r.read(), b'resumed\n')
                        self.assertEqual(line(), b'RETIRED')
                    command('wait')
                    outputs = [line()]
                    if outputs[0] == b'RETIRED': outputs.append(line())
                    self.assertEqual(outputs[-1], b'completed' if release else b'deadline_expired')
                command('stop'); self.assertEqual(child.wait(timeout=3), 0)

    @unittest.skipIf(os.environ.get('V3_TLS_MODE') == 'yes', 'TLS-on build')
    def test_tls_off_examples_exercise_typed_operations(self):
        for name in ['v3_tls_profiles', 'v3_tls_rotation', 'v3_http2', 'v3_http3']:
            binary = EXAMPLES / name
            self.assertTrue(binary.is_file(), f'missing compiled example: {binary}')
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('listen=not_supported port=0', result.stdout)
            self.assertIn('replace=not_supported', result.stdout)

    @unittest.skipUnless(os.environ.get('V3_TLS_MODE') == 'yes', 'TLS-off build')
    def test_tls_profiles_certificate_mtls_psk_acme(self):
        fixtures = Path(os.environ.get('V3_TLS_FIXTURES', Path(__file__).resolve().parents[1] / 'data/tls_credentials'))
        with tempfile.TemporaryDirectory() as directory:
            chains = []
            for name in ['a', 'b']:
                chain = Path(directory) / (name + '-chain.pem')
                chain.write_bytes((fixtures / (name + '.pem')).read_bytes() + (fixtures / 'intermediate.pem').read_bytes())
                chains.append(str(chain))
            with controlled('v3_tls_profiles', 'certificate', chains[0], str(fixtures / 'a-key.pem'), chains[1], str(fixtures / 'b-key.pem')) as (child, port, command, line):
                for host in ['a.example', 'b.example']:
                    context = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
                    with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname=host) as sock:
                        sock.sendall(b'GET /hello HTTP/1.1\r\nHost: example\r\n\r\n')
                        r = http.client.HTTPResponse(sock); r.begin(); self.assertEqual(r.status, 200); r.read()
                for host in ['unknown.example', None]:
                    context = ssl.create_default_context(cafile=str(fixtures / 'root.pem')); context.check_hostname = False
                    with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname=host) as sock:
                        self.assertIn(('DNS', 'a.example'), sock.getpeercert()['subjectAltName'])
                command('stop'); self.assertEqual(child.wait(timeout=3), 0)
            client_chain = Path(directory) / 'client-chain.pem'
            client_chain.write_bytes((fixtures / 'client-valid.pem').read_bytes() + (fixtures / 'client-intermediate.pem').read_bytes())
            for mode in ['request', 'require']:
                with controlled('v3_tls_profiles', 'mtls', chains[0], str(fixtures / 'a-key.pem'), str(fixtures / 'client-root.pem'), mode) as (child, port, command, line):
                    context = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
                    context.load_cert_chain(str(client_chain), str(fixtures / 'client-valid-key.pem'))
                    with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname='a.example') as sock:
                        sock.sendall(b'GET /hello HTTP/1.1\r\nHost: example\r\n\r\n')
                        r = http.client.HTTPResponse(sock); r.begin(); self.assertIn(b'verified=1', r.read())
                    anonymous = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
                    try:
                        with anonymous.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname='a.example') as sock:
                            sock.sendall(b'GET /hello HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n')
                            r = http.client.HTTPResponse(sock); r.begin()
                            self.assertEqual(mode, 'request', 'required client certificate was not enforced')
                            self.assertIn(b'verified=0', r.read())
                    except (ssl.SSLError, http.client.RemoteDisconnected, ConnectionResetError):
                        self.assertEqual(mode, 'require')
                    command('stop'); self.assertEqual(child.wait(timeout=3), 0)
            with controlled('v3_tls_profiles', 'psk', 'identity', '42' * 32) as (child, port, command, line):
                openssl = os.environ.get('V3_OPENSSL', 'openssl')
                for identity, success in [('identity', True), ('unknown', False)]:
                    r = subprocess.run([openssl, 's_client', '-connect', f'127.0.0.1:{port}', '-servername', 'a.example',
                         '-tls1_2', '-cipher', 'PSK-AES128-GCM-SHA256', '-psk_identity', identity, '-psk', '42' * 32, '-quiet'],
                         input=b'GET /hello HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n', capture_output=True, timeout=5)
                    self.assertEqual(b'profile-ok' in r.stdout, success, r.stderr)
                command('stop'); self.assertEqual(child.wait(timeout=3), 0)
            acme_cert, acme_key = Path(directory) / 'acme.pem', Path(directory) / 'acme-key.pem'
            subprocess.run([os.environ.get('V3_OPENSSL', 'openssl'), 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                '-config', os.devnull, '-subj', '/CN=a.example', '-days', '1', '-addext', 'subjectAltName=DNS:a.example',
                '-addext', '1.3.6.1.5.5.7.1.31=critical,DER:04:20:' + ':'.join(['42'] * 32),
                '-keyout', str(acme_key), '-out', str(acme_cert)], capture_output=True, timeout=10, check=True)
            with controlled('v3_tls_profiles', 'acme', chains[0], str(fixtures / 'a-key.pem'), str(acme_cert), str(acme_key), '42' * 32) as (child, port, command, line):
                command('publish'); self.assertEqual(line(), b'PUBLISHED')
                command('invalid'); self.assertEqual(line(), b'PRESERVED')
                ordinary = ssl.create_default_context(cafile=str(fixtures / 'root.pem')); ordinary.set_alpn_protocols(['http/1.1'])
                with ordinary.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname='a.example') as sock:
                    sock.sendall(b'GET /hello HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n')
                    r = http.client.HTTPResponse(sock); r.begin(); self.assertIn(b'profile-ok', r.read())
                # Deliberately ignore certificate trust for this negative ALPN probe:
                # erroneous challenge selection must remain observable, even though
                # the ephemeral self-signed challenge would fail ordinary CA checks.
                context = ssl._create_unverified_context(); context.set_alpn_protocols(['acme-tls/1'])
                with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
                    try:
                        sock = context.wrap_socket(raw, server_hostname='a.example')
                    except ssl.SSLError:
                        pass  # No common ordinary ALPN is an expected refusal.
                    else:
                        with sock: self.assertNotEqual(sock.selected_alpn_protocol(), 'acme-tls/1')
                command('remove'); self.assertEqual(line(), b'REMOVED')
                command('stop'); self.assertEqual(child.wait(timeout=3), 0)

    @unittest.skipUnless(os.environ.get('V3_TLS_MODE') == 'yes' and importlib.util.find_spec('h2'), 'TLS/hyper-h2 client unavailable')
    def test_http2_hello_and_extended_connect(self):
        import h2.config
        import h2.connection
        import h2.events
        fixtures = Path(os.environ.get('V3_TLS_FIXTURES', Path(__file__).resolve().parents[1] / 'data/tls_credentials'))
        with tempfile.TemporaryDirectory() as directory:
            chain = Path(directory) / 'chain.pem'; chain.write_bytes((fixtures / 'a.pem').read_bytes() + (fixtures / 'intermediate.pem').read_bytes())
            with controlled('v3_http2', str(chain), str(fixtures / 'a-key.pem'), numeric=True) as (child, port, command, line):
                context = ssl.create_default_context(cafile=str(fixtures / 'root.pem')); context.set_alpn_protocols(['h2'])
                with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname='a.example') as sock:
                    self.assertEqual(sock.selected_alpn_protocol(), 'h2')
                    client = h2.connection.H2Connection(h2.config.H2Configuration(header_encoding='utf-8')); client.initiate_connection()
                    sock.sendall(client.data_to_send())
                    def until(predicate):
                        result = []
                        deadline = time.monotonic() + 3
                        while not predicate(result):
                            self.assertLess(time.monotonic(), deadline)
                            events = client.receive_data(sock.recv(65536)); result += events
                            for event in events:
                                if isinstance(event, h2.events.DataReceived): client.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                            data = client.data_to_send()
                            if data: sock.sendall(data)
                        return result
                    until(lambda _: client.remote_settings.enable_connect_protocol == 1)
                    client.send_headers(1, [(':method', 'GET'), (':scheme', 'https'), (':authority', 'a.example'), (':path', '/hello')], end_stream=True)
                    sock.sendall(client.data_to_send()); events = until(lambda events: any(isinstance(e, h2.events.StreamEnded) for e in events))
                    self.assertEqual(b''.join(e.data for e in events if isinstance(e, h2.events.DataReceived)), b'hello h2\n')
                    client.send_headers(3, [(':method', 'CONNECT'), (':protocol', 'websocket'), (':scheme', 'https'), (':authority', 'a.example'), (':path', '/ws/echo'), ('sec-websocket-version', '13'), ('sec-websocket-protocol', 'harness.v1')], end_stream=False)
                    sock.sendall(client.data_to_send()); events = until(lambda events: any(isinstance(e, h2.events.ResponseReceived) for e in events))
                    self.assertIn((':status', '200'), next(e.headers for e in events if isinstance(e, h2.events.ResponseReceived)))
                    from http2_client_h2 import masked
                    for opcode, payload in [(1, b'hello h2'), (2, b'\0\xff')]:
                        client.send_data(3, masked(opcode, payload)); sock.sendall(client.data_to_send())
                        events = until(lambda events: sum(len(e.data) for e in events if isinstance(e, h2.events.DataReceived)) >= len(payload) + 2)
                        self.assertEqual(b''.join(e.data for e in events if isinstance(e, h2.events.DataReceived)), bytes([128 | opcode, len(payload)]) + payload)
                    # Zero receive credit and more data than the example's bounded
                    # outgoing queues force a stall; restoring credit resumes echoes.
                    import h2.settings
                    client.update_settings({h2.settings.SettingCodes.INITIAL_WINDOW_SIZE: 0})
                    sock.sendall(client.data_to_send())
                    until(lambda events: any(isinstance(e, h2.events.SettingsAcknowledged) for e in events))
                    payload = b'x' * 8000; mask = b'\1\2\3\4'
                    wire = b'\x82\xfe' + len(payload).to_bytes(2, 'big') + mask + bytes(v ^ mask[i % 4] for i, v in enumerate(payload))
                    for _ in range(6): client.send_data(3, wire)
                    client.ping(b'pause123'); sock.sendall(client.data_to_send())
                    paused = until(lambda events: any(isinstance(e, h2.events.PingAckReceived) for e in events))
                    self.assertFalse(any(isinstance(e, h2.events.DataReceived) for e in paused))
                    client.increment_flow_control_window(65535, 3); sock.sendall(client.data_to_send())
                    expected = (b'\x82\x7e' + len(payload).to_bytes(2, 'big') + payload) * 6
                    resumed = until(lambda events: sum(len(e.data) for e in events if isinstance(e, h2.events.DataReceived)) >= len(expected))
                    self.assertEqual(b''.join(e.data for e in resumed if isinstance(e, h2.events.DataReceived)), expected)
                    client.send_data(3, masked(8, b'\x03\xe8')); sock.sendall(client.data_to_send())
                    until(lambda events: any(isinstance(e, h2.events.DataReceived) for e in events))
                command('stop'); self.assertEqual(child.wait(timeout=3), 0)

    @unittest.skipUnless(os.environ.get('V3_TLS_MODE') == 'yes' and importlib.util.find_spec('aioquic'), 'TLS/aioquic client unavailable')
    def test_http3_hello_trailers_and_extended_connect_both_loops(self):
        from aioquic.asyncio import connect
        from aioquic.quic.configuration import QuicConfiguration
        from http3_client_aioquic import Client
        from http3_websocket_aioquic import WebSocketClient, verify_upgrade
        from http3_websocket_wire import frame, expect
        fixtures = Path(os.environ.get('V3_TLS_FIXTURES', Path(__file__).resolve().parents[1] / 'data/tls_credentials'))
        async def probe(port):
            config = QuicConfiguration(is_client=True, alpn_protocols=['h3']); config.server_name = 'a.example'
            config.load_verify_locations(cafile=str(fixtures / 'root.pem'))
            async with connect('127.0.0.1', port, configuration=config, create_protocol=Client) as client:
                state = client.request('GET', '/hello'); result = await asyncio.wait_for(state['done'], 5)
                self.assertEqual(bytes(result['body']), b'hello native h3\n')
                self.assertIn((b'x-end', b'native-fin'), result['headers'])
            async with connect('127.0.0.1', port, configuration=config, create_protocol=WebSocketClient) as client:
                await asyncio.wait_for(client.h3.settings_ready.wait(), 5)
                state = client.open_ws('/ws/echo'); verify_upgrade(await asyncio.wait_for(state.headers, 5))
                for opcode, payload in [(1, b'hello h3'), (2, b'\0\xff')]:
                    client.send(state, frame(opcode, payload)); expect(await state.next_frame(), opcode, payload)
                client.send(state, frame(8, b'\x03\xe8'), end=True); expect(await state.next_frame(), 8, b'\x03\xe8')
                await asyncio.wait_for(state.terminal, 5)
        with tempfile.TemporaryDirectory() as directory:
            chain = Path(directory) / 'chain.pem'; chain.write_bytes((fixtures / 'a.pem').read_bytes() + (fixtures / 'intermediate.pem').read_bytes())
            for mode in ['managed', 'external']:
                with controlled('v3_http3', str(chain), str(fixtures / 'a-key.pem'), mode, numeric=True) as (child, port, command, line):
                    self.assertTrue(line().startswith(b'TCP_PORT '))
                    asyncio.run(asyncio.wait_for(probe(port), 20))
                    command('stop'); self.assertEqual(child.wait(timeout=3), 0)

    def test_bounded_hosts_report_invalid_duration(self):
        for name in ['v3_routes', 'v3_request_data', 'v3_auth', 'v3_body_streaming', 'v3_websocket_echo']:
            result = subprocess.run([str(EXAMPLES / name), 'invalid'], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertIn('usage:', result.stderr)

    def test_protocol_hosts_reject_invalid_loop_mode(self):
        for name, arguments in [('v3_http3', ['unused-chain', 'unused-key', 'invalid']),
                                ('v3_tls_rotation', ['unused-directory', 'invalid', '0'])]:
            result = subprocess.run([str(EXAMPLES / name), *arguments], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

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
        args = [str(binary)]
        tls = os.environ.get('V3_TLS_MODE') == 'yes'
        if tls:
            fixtures = Path(os.environ.get('V3_TLS_FIXTURES', Path(__file__).resolve().parents[1] / 'data/tls_credentials'))
            args += [str(fixtures / 'a.pem'), str(fixtures / 'a-key.pem')]
        result = subprocess.run(args, capture_output=True, timeout=5,
                                check=False, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('HTTP/1.1 validate=ok listen=ok', result.stdout)
        for version in ('TLS', 'HTTP/2', 'HTTP/3'):
            self.assertRegex(result.stdout, version + (r' validate=ok listen=ok port=[1-9][0-9]*' if tls else r' validate=(ok|not_supported) listen=not_supported port=0'))

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
    def test_public_rotation_demonstration(self):
        binary = Path(os.environ.get('V3_ROTATION_EXAMPLE', EXAMPLES / 'v3_tls_rotation')).resolve()
        self.assertTrue(binary.is_file(), f'missing rotation demonstration: {binary}')
        fixtures = Path(os.environ.get('V3_TLS_FIXTURES', Path(__file__).resolve().parents[1] / 'data/tls_credentials'))
        context = ssl.create_default_context(cafile=str(fixtures / 'root.pem'))
        context.set_alpn_protocols(['http/1.1'])
        with subprocess.Popen([str(binary), str(fixtures), 'managed', '0'],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as child:
            lines = queue.Queue(maxsize=4)
            def read_lines():
                for line in child.stdout:
                    lines.put(line)
            reader = threading.Thread(target=read_lines, daemon=True)
            reader.start()
            try:
                port = int(lines.get(timeout=5))
                self.assertGreater(port, 0)
                def hello(sock):
                    sock.sendall(b'GET /installed HTTP/1.1\r\nHost: example\r\n\r\n')
                    reply = http.client.HTTPResponse(sock)
                    reply.begin()
                    self.assertEqual((reply.status, reply.read()), (200, b'rotation-example\n'))
                with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3),
                                         server_hostname='a.example') as established:
                    hello(established)
                    child.stdin.write(b'invalid\n'); child.stdin.flush()
                    self.assertEqual(lines.get(timeout=3), b'PRESERVED\n')
                    hello(established)
                    child.stdin.write(b'replace\n'); child.stdin.flush()
                    self.assertEqual(lines.get(timeout=3), b'REPLACED\n')
                    hello(established)
                    with context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3),
                                             server_hostname='b.example') as replacement:
                        hello(replacement)
                child.stdin.write(b'stop\n'); child.stdin.flush()
                self.assertEqual(child.wait(timeout=3), 0)
            finally:
                if child.poll() is None:
                    child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait(timeout=3)
                reader.join(timeout=1)


if __name__ == '__main__':
    unittest.main()
