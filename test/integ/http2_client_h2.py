#!/usr/bin/env python3
"""Pinned hyper-h2 4.4.1 over real TLS; RFC 8441 is supported natively.

Only RFC 6455 payload framing is implemented here. HTTP/2 and HPACK use h2.
"""
import argparse
import json
import select
import socket
import ssl
import subprocess
import time
from importlib.metadata import version
import h2.config
import h2.connection
import h2.events
import h2.settings

SCENARIOS = ['multiplexing', 'reset', 'upload_flow', 'response_flow', 'websocket']


def masked(opcode, data):
    assert len(data) < 126
    mask = b'\x01\x02\x03\x04'
    return bytes([128 | opcode, 128 | len(data)]) + mask + bytes(x ^ mask[i % 4] for i, x in enumerate(data))


class Client:
    def __init__(self, fixture):
        self.proc = subprocess.Popen([fixture], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=None, text=True, bufsize=1)
        self.sock = None

    def connect(self):
        if not select.select([self.proc.stdout], [], [], 5)[0]:
            raise RuntimeError('fixture readiness timeout')
        line = self.proc.stdout.readline().split()
        assert len(line) == 2 and line[0] == 'READY', line
        self.port = int(line[1])
        context = ssl._create_unverified_context()
        context.set_alpn_protocols(['h2'])
        self.sock = context.wrap_socket(socket.create_connection(('127.0.0.1', self.port), timeout=5),
                                        server_hostname='a.example')
        assert self.sock.selected_alpn_protocol() == 'h2'
        self.sock.settimeout(.2)
        self.h2 = h2.connection.H2Connection(h2.config.H2Configuration(header_encoding='utf-8'))
        self.h2.initiate_connection()
        self.h2.update_settings({h2.settings.SettingCodes.INITIAL_WINDOW_SIZE: 32})
        self.events = []; self.data = {}; self.ended = set(); self.status = {}; self.pings = set()
        self.flush()
        self.wait(lambda: self.h2.remote_settings.enable_connect_protocol == 1)

    def close(self):
        if self.sock is not None:
            self.sock.close()
        if self.proc.poll() is None:
            try:
                self.proc.stdin.write('STOP\n'); self.proc.stdin.flush()
            except BrokenPipeError:
                pass
        try:
            code = self.proc.wait(timeout=5)
            assert code == 0, code
        finally:
            if self.proc.poll() is None:
                self.proc.kill(); self.proc.wait()

    def command(self, command):
        self.proc.stdin.write(command + '\n'); self.proc.stdin.flush()

    def stats(self):
        self.command('STATS')
        if not select.select([self.proc.stdout], [], [], 3)[0]:
            raise RuntimeError('STATS timeout')
        line = self.proc.stdout.readline().split()
        assert len(line) == 6 and line[0] == 'STATS', line
        return list(map(int, line[1:]))

    def flush(self):
        self.sock.sendall(self.h2.data_to_send())

    def pump(self):
        try:
            data = self.sock.recv(65536)
        except socket.timeout:
            return
        assert data, 'TLS EOF'
        for event in self.h2.receive_data(data):
            self.events.append(event)
            if isinstance(event, h2.events.DataReceived):
                self.data.setdefault(event.stream_id, bytearray()).extend(event.data)
                # Connection credit is independent; stream grants are explicit.
                if event.flow_controlled_length:
                    self.h2.increment_flow_control_window(event.flow_controlled_length)
            if isinstance(event, h2.events.ResponseReceived):
                self.status[event.stream_id] = dict(event.headers)[':status']
            if isinstance(event, h2.events.StreamEnded):
                self.ended.add(event.stream_id)
            if isinstance(event, h2.events.PingAckReceived):
                self.pings.add(event.ping_data)
            if isinstance(event, h2.events.ConnectionTerminated):
                raise RuntimeError('GOAWAY: ' + repr(event))
        self.flush()

    def wait(self, condition):
        deadline = time.monotonic() + 5
        while not condition():
            if time.monotonic() >= deadline:
                raise RuntimeError('client condition timed out')
            self.pump()

    def barrier(self):
        tag = len(self.pings).to_bytes(8, 'big')
        self.h2.ping(tag); self.flush(); self.wait(lambda: tag in self.pings)

    def request(self, path, method='GET', end=True, extra=()):
        stream = self.h2.get_next_available_stream_id()
        headers = [(':method', method), (':scheme', 'https'), (':authority', 'a.example'), (':path', path)] + list(extra)
        self.h2.send_headers(stream, headers, end_stream=end); self.flush()
        return stream

    def health(self):
        stream = self.request('/health'); self.wait(lambda: stream in self.ended)
        assert self.status[stream] == '204'

    def scenarios(self):
        held = self.request('/hold'); self.health(); self.barrier()
        assert held not in self.status
        self.command('RELEASE'); self.wait(lambda: held in self.ended)
        assert self.status[held] == '200'
        # Upload routes have a separate unreleased suspension signal.
        reset = self.request('/upload-hold', 'POST', False)
        self.barrier(); before = self.stats()[2]
        self.h2.reset_stream(reset, error_code=8); self.flush(); self.health(); self.barrier()
        assert self.stats()[2] == before + 1
        self.health(); self.barrier(); assert self.stats()[2] == before + 1
        uploads = [self.request('/upload-hold', 'POST', False,
                                [('content-length', '32768')]) for _ in range(5)]
        sent = dict.fromkeys(uploads, 0)
        def transfer():
            progressed = False
            for stream in uploads:
                n = min(32768 - sent[stream], self.h2.local_flow_control_window(stream), self.h2.max_outbound_frame_size)
                if n > 0:
                    self.h2.send_data(stream, b'U' * n, end_stream=sent[stream] + n == 32768)
                    sent[stream] += n; progressed = True
            self.flush(); return progressed
        self.barrier()
        while transfer():
            self.barrier()
        self.barrier(); stalled = self.stats()
        assert 0 < sum(sent.values()) <= 65535 and max(sent.values()) <= 16384, sent
        assert stalled[3] == 0 and stalled[4] == sum(sent.values()), stalled
        self.command('UPLOAD')
        self.wait(lambda: transfer() or any(self.h2.local_flow_control_window(s) > 0 for s in uploads if sent[s] < 32768))
        deadline = time.monotonic() + 8
        while not all(s in self.ended for s in uploads):
            assert time.monotonic() < deadline
            transfer(); self.pump()
        assert all(self.status[s] == '200' for s in uploads)
        assert self.stats()[3] == 5 * 32768
        large = self.request('/large'); self.wait(lambda: len(self.data.get(large, b'')) == 32)
        self.barrier(); assert large not in self.ended and len(self.data[large]) == 32
        self.h2.increment_flow_control_window(131072, stream_id=large); self.flush()
        self.wait(lambda: large in self.ended)
        assert self.data[large] == b'L' * 131072
        ws = self.request('/ws', 'CONNECT', False,
                          [(':protocol', 'websocket'), ('sec-websocket-version', '13')])
        self.wait(lambda: ws in self.status); assert self.status[ws] == '200' and ws not in self.ended
        self.health()
        expected = b''
        for opcode, payload in [(1, b'hello'), (2, b'\x00\xff'), (9, b'ping')]:
            self.h2.send_data(ws, masked(opcode, payload)); self.flush()
            expected += bytes([128 | (10 if opcode == 9 else opcode), len(payload)]) + payload
            self.wait(lambda: len(self.data.get(ws, b'')) >= len(expected))
            assert self.data[ws] == expected
        self.h2.increment_flow_control_window(128, stream_id=ws)
        self.h2.send_data(ws, masked(8, b'\x03\xe8'), end_stream=True); self.flush()
        self.wait(lambda: ws in self.ended)
        assert self.data[ws] == expected + b'\x88\x02\x03\xe8'
        self.health()
        return {'stack': 'hyper-h2', 'version': version('h2'), 'alpn': 'h2', 'scenarios': SCENARIOS,
                'upload_stalled_bytes': stalled[4], 'uploaded_bytes': 5 * 32768, 'response_stalled_bytes': 32,
                'response_bytes': 131072, 'cancel_count': 1, 'ws_clean_close': True}


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--fixture', required=True)
    args = parser.parse_args(); assert version('h2') == '4.4.1'
    client = Client(args.fixture)
    try:
        client.connect()
        result = client.scenarios()
    finally:
        client.close()
    print(json.dumps(result))


if __name__ == '__main__':
    main()
