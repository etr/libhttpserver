#!/usr/bin/env python3
"""Bounded public native H2 capacity proof using an installed consumer."""
import argparse
import asyncio
import json
import select
import socket
import ssl
import subprocess
import time
from pathlib import Path
import h2.config
import h2.connection
import h2.events
import h2.settings
from aioquic.asyncio import QuicConnectionProtocol, connect
from aioquic.h3.connection import H3Connection
from aioquic.h3.events import HeadersReceived
from aioquic.quic.configuration import QuicConfiguration


def require(value, message):
    if not value:
        raise AssertionError(message)


def line(child):
    require(select.select([child.stdout], [], [], 10)[0], 'consumer output deadline')
    value = child.stdout.readline().decode().strip()
    require(value, 'consumer exited')
    return value


def wait_count(child, wanted):
    deadline = time.monotonic() + 5
    observed = None
    while time.monotonic() < deadline:
        child.stdin.write(b'COUNTS\n'); child.stdin.flush()
        observed = int(line(child))
        if observed >= wanted:
            break
        time.sleep(.01)
    require(observed == wanted, f'native admitted request count: {observed}, wanted {wanted}')


class Client:
    def __init__(self, port):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        context.set_alpn_protocols(['h2'])
        self.socket = context.wrap_socket(socket.create_connection(('127.0.0.1', port), 5), server_hostname='a.example')
        self.socket.settimeout(5)
        self.connection = h2.connection.H2Connection(config=h2.config.H2Configuration(client_side=True))
        self.connection.initiate_connection()
        self.socket.sendall(self.connection.data_to_send())
        self.events = []
        self.receive()
        self.connection.max_inbound_frame_size = 16384

    def receive(self):
        data = self.socket.recv(262144)
        require(data, 'unexpected connection close')
        events = self.connection.receive_data(data)
        self.events.extend(events)
        output = self.connection.data_to_send()
        if output:
            self.socket.sendall(output)
        return events

    def request(self, stream, path, extra=()):
        self.connection.send_headers(stream, [(':method', 'GET'), (':scheme', 'https'), (':authority', 'a.example'), (':path', path), *extra], end_stream=True)
        self.socket.sendall(self.connection.data_to_send())

    def ended(self, stream):
        while not any(isinstance(event, (h2.events.StreamEnded, h2.events.StreamReset, h2.events.ConnectionTerminated)) and getattr(event, 'stream_id', stream) == stream for event in self.events):
            self.receive()
        return any(isinstance(event, h2.events.StreamEnded) and event.stream_id == stream for event in self.events)


class H3Protocol(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.http = H3Connection(self._quic)
        self.completed = set()
        self.responses = {}

    def quic_event_received(self, event):
        for http_event in self.http.handle_event(event):
            if isinstance(http_event, HeadersReceived):
                self.responses[http_event.stream_id] = http_event.headers
                if http_event.stream_ended:
                    self.completed.add(http_event.stream_id)


async def h3_capacity(port, child):
    configuration = QuicConfiguration(is_client=True, alpn_protocols=['h3'], verify_mode=ssl.CERT_NONE)
    configuration.server_name = 'a.example'
    async with connect('127.0.0.1', port, configuration=configuration, create_protocol=H3Protocol) as client:
        deadline = time.monotonic() + 5
        while client.http.received_settings is None and time.monotonic() < deadline:
            await asyncio.sleep(.01)
        settings = client.http.received_settings
        require(settings is not None, 'native H3 SETTINGS missing')
        require(settings.get(1) == 8192 and settings.get(7) == 32, f'H3 SETTINGS {settings}')
        require(6 not in settings, 'unexpected H3 advertised header maximum')
        require(client._quic._remote_max_streams_bidi == 24, 'native H3 initial bidi credit')
        require(client._quic._remote_max_streams_uni == 8, 'native H3 initial uni credit')
        for n in range(24):
            stream = client._quic.get_next_available_stream_id()
            client.http.send_headers(stream, [(b':method', b'GET'), (b':scheme', b'https'), (b':authority', b'a.example'), (b':path', b'/hold')], end_stream=True)
        client.transmit()
        deadline = time.monotonic() + 5
        observed = None
        while time.monotonic() < deadline:
            await asyncio.sleep(.01)
            child.stdin.write(b'COUNTS\n'); child.stdin.flush()
            observed = int(line(child))
            if observed >= 216:
                break
        require(observed == 216, f'native H3 request admission above16: {observed}')
        child.stdin.write(b'RELEASE\n'); child.stdin.flush()
        require(line(child) == 'RELEASED', 'H3 release')
        deadline = time.monotonic() + 5
        while len(client.completed) < 24 and time.monotonic() < deadline:
            await asyncio.sleep(.01)
        require(len(client.completed) == 24, 'native H3 held requests incomplete')
        stream = client._quic.get_next_available_stream_id()
        headers = [(b':method', b'GET'), (b':scheme', b'https'), (b':authority', b'a.example'), (b':path', b'/headers')]
        headers += [(('x-growth-' + str(n)).encode(), b'v') for n in range(140)]
        client.http.send_headers(stream, headers, end_stream=True)
        client.transmit()
        deadline = time.monotonic() + 5
        while stream not in client.completed and time.monotonic() < deadline:
            await asyncio.sleep(.01)
        require(stream in client.completed and (b':status', b'204') in client.responses[stream], 'native H3 header growth')
        return dict(decoder_table_bytes=settings[1], max_blocked_headers=settings[7], max_field_section_bytes=None, initial_bidi=24, initial_uni=8, concurrent_requests=24, fields_above_default=True)


def run(executable, fixtures, loop):
    child = subprocess.Popen([str(executable), str(fixtures), loop], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    clients = []
    try:
        ready = line(child)
        if ready == 'TLS_OFF_CAPACITY_REJECTED':
            child.wait(timeout=10)
            require(child.returncode == 0, 'TLS-off consumer')
            return dict(loop=loop, unavailable=True)
        require(ready.startswith('PORT '), ready)
        port = int(ready.split()[1])
        client = Client(port)
        clients.append(client)
        settings = client.connection.remote_settings
        require(settings.max_concurrent_streams == 192, f'wire request cap {settings.max_concurrent_streams}')
        require(settings.header_table_size == 8192, f'wire decoder cap {settings.header_table_size}')
        require(settings.max_header_list_size is None, 'unexpected advertised header maximum')
        # Hold 192 requests concurrently, crossing the independent opening-rate
        # interval before each batch. This exercises admission above the old128.
        for first, last in ((0, 96), (96, 192)):
            if first:
                time.sleep(1.1)
            for request in range(first, last):
                client.request(2 * request + 1, '/hold')
            wait_count(child, last)
        wait_count(child, 192)
        child.stdin.write(b'INSPECT\n'); child.stdin.flush()
        require(line(child) == '192 24', 'immutable mixed policy inspection')
        child.stdin.write(b'RELEASE\n'); child.stdin.flush()
        require(line(child) == 'RELEASED', 'release')
        require(client.ended(383), 'held request completion')
        client.socket.close()
        client = Client(port); clients.append(client)
        # expanded bytes exceed65536 and occurrence count exceeds256.
        client.request(1, '/headers', [('x-large', 'v' * 57000), *[(f'x-{n}', 'v') for n in range(300)]])
        require(client.ended(1), 'above-default header bytes/fields rejected')
        client.socket.close()
        client = Client(port); clients.append(client)
        client.request(1, '/headers', [(f'x-{n}', 'v') for n in range(509)])
        require(not client.ended(1), 'field count one beyond512 accepted')
        h3 = asyncio.run(h3_capacity(int(ready.split()[2]), child))
        return dict(loop=loop, http3=h3, settings=dict(max_concurrent_requests=192, decoder_table_bytes=8192, max_header_list_bytes=None), concurrent_requests=192, expanded_above_default=True, fields_above_default=True, field_boundary_rejected=True)
    finally:
        for client in clients:
            client.socket.close()
        if child.poll() is None:
            child.stdin.write(b'STOP\n'); child.stdin.flush()
        try:
            child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            child.kill(); child.wait()
            raise AssertionError('consumer failed to stop')
        require(child.returncode == 0, f'consumer exit {child.returncode}: {child.stderr.read().decode()}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = [run(args.consumer, args.fixtures, loop) for loop in ('managed', 'external')]
    args.output.write_text(json.dumps(rows, indent=2) + '\n')


if __name__ == '__main__':
    main()
