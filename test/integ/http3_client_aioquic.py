#!/usr/bin/env python3
"""Independent aioquic network probe; successful handshakes verify the fixture CA."""
import argparse
import asyncio
import json
import ssl
import sys
import os
from pathlib import Path
import aioquic
from aioquic.asyncio import connect, QuicConnectionProtocol
from aioquic.h3.connection import H3Connection
from aioquic.h3.events import HeadersReceived, DataReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted, StreamReset
from aioquic.quic.logger import QuicFileLogger


class StreamFailure(Exception):
    def __init__(self, event):
        self.event = event
        super().__init__(type(event).__name__)


class Client(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.h3 = H3Connection(self._quic)
        self.pending = {}
        self.handshake = None
        self.termination = None

    def quic_event_received(self, event):
        if isinstance(event, HandshakeCompleted):
            self.handshake = event
        if isinstance(event, ConnectionTerminated):
            self.termination = event
        if isinstance(event, (StreamReset, ConnectionTerminated)):
            for stream, state in list(self.pending.items()):
                if isinstance(event, ConnectionTerminated) or event.stream_id == stream:
                    if not state['done'].done():
                        state['done'].set_exception(StreamFailure(event))
        for item in self.h3.handle_event(event):
            if isinstance(item, (HeadersReceived, DataReceived)) and item.stream_id in self.pending:
                state = self.pending[item.stream_id]
                if isinstance(item, HeadersReceived):
                    state['headers'].extend(item.headers)
                else:
                    state['body'].extend(item.data)
                if item.stream_ended and not state['done'].done():
                    state['done'].set_result(state)

    def request(self, method, path, body=b''):
        stream = self._quic.get_next_available_stream_id()
        state = {'done': asyncio.get_running_loop().create_future(), 'headers': [], 'body': bytearray(), 'stream': stream}
        self.pending[stream] = state
        fields = [(b':method', method.encode()), (b':scheme', b'https'), (b':authority', b'localhost'), (b':path', path.encode())]
        if body:
            fields.append((b'content-length', str(len(body)).encode()))
        self.h3.send_headers(stream, fields, end_stream=not body)
        for offset in range(0, len(body), 3):
            part = body[offset:offset + 3]
            self.h3.send_data(stream, part, end_stream=offset + len(part) == len(body))
        self.transmit()
        return state


UPLOAD = b'first\x00second\xffthird\x00'


async def control(command, **fields):
    print(json.dumps({'command': command, **fields}), flush=True)
    line = await asyncio.to_thread(sys.stdin.readline)
    if not line:
        raise RuntimeError('runner control EOF')
    return json.loads(line)


def response(state):
    return {'status': int(dict(state['headers'])[b':status']), 'body_hex': bytes(state['body']).hex(), 'stream': state['stream']}


async def main(args):
    config = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1])
    config.load_verify_locations(cafile=args.ca)
    config.server_name = 'localhost'
    config.verify_mode = ssl.CERT_REQUIRED
    Path(args.log_dir).mkdir(parents=True, exist_ok=True)
    config.quic_logger = QuicFileLogger(args.log_dir)
    cases = []
    key_path = Path(args.log_dir) / 'tls.keys'
    key_fd = os.open(key_path, os.O_CREAT | os.O_TRUNC | os.O_WRONLY, 0o600)
    with os.fdopen(key_fd, 'w') as keys:
        config.secrets_log_file = keys
        async with connect('127.0.0.1', args.port, configuration=config, create_protocol=Client, wait_connected=True) as client:
            h = client.handshake
            cases.append({'case': 'handshake', 'alpn': h.alpn_protocol, 'quic_version': client._quic._version, 'tls_version': 'TLSv1.3', 'verified': config.verify_mode == ssl.CERT_REQUIRED, 'early_data': h.early_data_accepted})
            for name, method, path, body in [('get', 'GET', '/hello', b''), ('post', 'POST', '/echo', UPLOAD)]:
                result = await asyncio.wait_for(client.request(method, path, body)['done'], 15)
                cases.append({'case': name, **response(result)})
            held = client.request('GET', '/hold')
            observed = await control('wait', event='held', stream=held['stream'])
            health = await asyncio.wait_for(client.request('GET', '/health')['done'], 15)
            overlap = not held['done'].done()
            await control('release', stream=held['stream'])
            released = await asyncio.wait_for(held['done'], 15)
            cases.append({'case': 'concurrency', 'held_stream': held['stream'], 'health_stream': health['stream'], 'health_status': response(health)['status'], 'hold_status': response(released)['status'], 'health_before_release': overlap, 'connection': observed['connection']})
            await control('reset')
            cancelled = client.request('GET', '/hold')
            await control('wait', event='held', stream=cancelled['stream'])
            client._quic.stop_stream(cancelled['stream'], 0x10c)
            client._quic.reset_stream(cancelled['stream'], 0x10c)
            client.transmit()
            try:
                await asyncio.wait_for(cancelled['done'], 15)
                raise AssertionError('cancelled request succeeded')
            except StreamFailure as failure:
                assert isinstance(failure.event, StreamReset)
                code = failure.event.error_code
            observed = await control('wait', event='cancelled', stream=cancelled['stream'])
            sibling = await asyncio.wait_for(client.request('GET', '/hello')['done'], 15)
            cases.append({'case': 'cancellation', 'stream': cancelled['stream'], 'type': 'StreamReset', 'code': code, 'sibling_status': response(sibling)['status'], 'server_cancelled': observed['event'] == 'cancelled'})
            missing = await asyncio.wait_for(client.request('GET', '/missing')['done'], 15)
            cases.append({'case': 'missing', 'status': response(missing)['status']})
        wrong = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1], server_name='localhost')
        wrong.load_verify_locations(cafile=args.wrong_ca)
        wrong.verify_mode = ssl.CERT_REQUIRED
        wrong.secrets_log_file = keys
        wrong.quic_logger = config.quic_logger
        attempts = []
        def protocol(*a, **kw):
            p = Client(*a, **kw); attempts.append(p); return p
        try:
            async with connect('127.0.0.1', args.port, configuration=wrong, create_protocol=protocol, wait_connected=True):
                raise AssertionError('wrong CA succeeded')
        except ConnectionError:
            event = attempts[0].termination
            assert event and event.error_code in (298, 304), 'verification TLS alert required'
            cases.append({'case': 'tls_failure', 'type': 'ConnectionTerminated', 'code': event.error_code, 'response': False})
    receipt = {'client': 'aioquic', 'version': aioquic.__version__, 'cases': cases}
    print(json.dumps({'receipt': receipt}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--ca', required=True)
    parser.add_argument('--log-dir', required=True)
    parser.add_argument('--wrong-ca', required=True)
    asyncio.run(main(parser.parse_args()))
