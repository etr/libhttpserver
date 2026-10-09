#!/usr/bin/env python3
"""Independent RFC 9220 network probe using pinned aioquic QUIC/TLS/QPACK/H3."""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import ssl

import aioquic
from aioquic.asyncio import connect
from aioquic.h3.connection import H3Connection, FrameType, Setting
from aioquic.h3.events import DataReceived, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted, StreamReset
from aioquic.quic.logger import QuicFileLogger
from http3_client_aioquic import StreamFailure, control, response
from http3_semantic_aioquic import SemanticClient
from http3_websocket_wire import Decoder, frame, expect, close_payload

TEXT = b'hello HTTP/3'
BINARY = b'\0\xff\x80binary\0'
FRAGMENT = 'fragment \u20ac done'.encode()
PING = b'probe\0\xff'


def future():
    result = asyncio.get_running_loop().create_future()
    # Terminal errors remain awaitable, even when the other wait wins first.
    result.add_done_callback(lambda f: f.exception() if not f.cancelled() else None)
    return result


class ObservedH3(H3Connection):
    def __init__(self, quic):
        super().__init__(quic)
        self.observed_settings = None
        self.settings_ready = asyncio.Event()

    def _handle_control_frame(self, kind, data):
        super()._handle_control_frame(kind, data)
        if kind == FrameType.SETTINGS:
            if not self._settings_received or not isinstance(self._received_settings, dict):
                raise RuntimeError('pinned SETTINGS observer unavailable')
            self.observed_settings = dict(self._received_settings)
            if self._quic_logger:
                self._quic_logger.log_event(category='http', event='settings_received', data={'settings': {str(k): v for k,v in self.observed_settings.items()}})
            self.settings_ready.set()


class WebSocketState:
    def __init__(self, stream):
        self.stream = stream
        self.headers = future()
        self.terminal = future()
        self.decoder = Decoder()
        self.frames = asyncio.Queue(maxsize=256)
        self.close_seen = False
        self.frame_count = 0

    async def next_frame(self):
        if not self.frames.empty():
            return self.frames.get_nowait()
        pending = asyncio.create_task(self.frames.get())
        try:
            ready, _ = await asyncio.wait([pending, self.terminal], timeout=5, return_when=asyncio.FIRST_COMPLETED)
            if pending in ready:
                return pending.result()
            if self.terminal in ready:
                await self.terminal
                raise RuntimeError('FIN before expected WebSocket frame')
            raise TimeoutError('WebSocket frame deadline')
        finally:
            if not pending.done():
                pending.cancel()
            await asyncio.gather(pending, return_exceptions=True)


class WebSocketClient(SemanticClient):
    def __init__(self, *args, **kwargs):
        if aioquic.__version__ != '1.3.0':
            raise RuntimeError('unsupported aioquic adapter version')
        super().__init__(*args, h3_factory=ObservedH3, **kwargs)
        if not callable(getattr(self._quic, '_write_stream_limits', None)):
            raise RuntimeError('pinned credit observer unavailable')
        self.websockets = {}
        self.server_connection = None

    def quic_event_received(self, event):
        if isinstance(event, HandshakeCompleted):
            self.handshake = event
        if isinstance(event, ConnectionTerminated):
            self.termination = event
        if isinstance(event, (StreamReset, ConnectionTerminated)):
            for stream, state in list(self.websockets.items()):
                if isinstance(event, ConnectionTerminated) or event.stream_id == stream:
                    for f in [state.headers, state.terminal]:
                        if not f.done(): f.set_exception(StreamFailure(event))
                    self.websockets.pop(stream)
            for stream, state in list(self.pending.items()):
                if isinstance(event, ConnectionTerminated) or event.stream_id == stream:
                    if not state['done'].done(): state['done'].set_exception(StreamFailure(event))
                    self.pending.pop(stream)
        for item in self.h3.handle_event(event):
            self.observe(item)

    def observe(self, item):
        state = self.websockets.get(getattr(item, 'stream_id', None))
        if state:
            if isinstance(item, HeadersReceived):
                if state.headers.done(): raise RuntimeError('unexpected tunnel trailers')
                state.headers.set_result(item.headers)
            elif isinstance(item, DataReceived):
                for observed in state.decoder.feed(item.data):
                    state.frame_count += 1
                    if state.frame_count > 256: raise RuntimeError('WebSocket frame count bound')
                    if observed.opcode == 8: state.close_seen = True
                    state.frames.put_nowait(observed)
            if item.stream_ended:
                state.decoder.finish()
                if not state.close_seen: raise RuntimeError('unexpected WebSocket FIN')
                state.terminal.set_result('FIN')
                self.websockets.pop(item.stream_id)
            return
        state = self.pending.get(getattr(item, 'stream_id', None))
        if state is None: return
        if isinstance(item, HeadersReceived):
            if state['headers']: raise RuntimeError('unexpected HTTP trailers')
            state['headers'].extend(item.headers)
        elif isinstance(item, DataReceived):
            state['body'].extend(item.data)
            if len(state['body']) > 65536: raise RuntimeError('HTTP sibling body bound')
        if item.stream_ended:
            state['done'].set_result(state)
            self.pending.pop(item.stream_id)

    def request(self, method, path, body=b''):
        if len(self.pending) + len(self.websockets) >= 16:
            raise RuntimeError('pending stream bound')
        state = super().request(method, path, body)
        state['done'].add_done_callback(lambda f: f.exception() if not f.cancelled() else None)
        return state

    def open_ws(self, path, *, freeze=False):
        if not self.h3.observed_settings or self.h3.observed_settings.get(Setting.ENABLE_CONNECT_PROTOCOL) != 1:
            raise RuntimeError('server has not enabled Extended CONNECT')
        if len(self.websockets) + len(self.pending) >= 16: raise RuntimeError('pending stream bound')
        stream = self._quic.get_next_available_stream_id()
        state = WebSocketState(stream)
        self.websockets[stream] = state
        if freeze: self.frozen.add(stream)
        self.h3.send_headers(stream, [(b':method', b'CONNECT'), (b':protocol', b'websocket'), (b':scheme', b'https'), (b':authority', b'localhost'), (b':path', path.encode()), (b'sec-websocket-version', b'13'), (b'sec-websocket-protocol', b'other, harness.v1'), (b'sec-websocket-extensions', b'permessage-deflate')], end_stream=False)
        self.transmit()
        return state

    def send(self, state, wire, *, end=False):
        self.h3.send_data(state.stream, wire, end_stream=end)
        self.transmit()


def verify_upgrade(headers):
    fields = {}
    for key, value in headers:
        if key in fields: raise AssertionError('duplicate upgrade field')
        fields[key] = value
    if fields.get(b':status') != b'200' or fields.get(b'sec-websocket-protocol') != b'harness.v1':
        raise AssertionError('Extended CONNECT status/subprotocol mismatch')
    if any(key in fields for key in [b'upgrade', b'connection', b'sec-websocket-extensions', b'sec-websocket-accept']):
        raise AssertionError('unexpected extension or HTTP/1 upgrade field')
    return [[k.decode(), v.decode()] for k, v in headers]


async def deadline(awaitable):
    return await asyncio.wait_for(asyncio.shield(awaitable), 5)


async def admit(client, path, **kwargs):
    state = client.open_ws(path, **kwargs)
    headers = verify_upgrade(await deadline(state.headers))
    witness = await control('wait', event='ws_accepted', stream=state.stream, **({'connection': client.server_connection} if client.server_connection is not None else {}))
    client.server_connection = witness['connection']
    return state, {'qlog_id': client._quic.original_destination_connection_id.hex(), 'stream': state.stream, 'connection': witness['connection'], 'headers': headers}


async def clean_close(client, state, *, initiated=True):
    payload = b'\x03\xe8'
    if initiated: client.send(state, frame(8, payload), end=True)
    observed = await state.next_frame()
    expect(observed, 8, payload)
    if not initiated: client.send(state, frame(8, payload), end=True)
    terminal = await deadline(state.terminal)
    witness = await control('wait', event='ws_retired', stream=state.stream, connection=client.server_connection)
    return {'close_hex': observed.payload.hex(), 'close_code': close_payload(observed.payload)[0], 'terminal': terminal, 'server': witness}


async def echo_cases(client, cases):
    await deadline(client.h3.settings_ready.wait())
    state, identity = await admit(client, '/ws/echo')
    h = client.handshake
    cases.append({'case': 'handshake', **identity, 'settings': {str(k): v for k,v in client.h3.observed_settings.items()}, 'alpn': h.alpn_protocol, 'quic_version': client._quic._version, 'tls_version': 'TLSv1.3', 'verified': client._quic.configuration.verify_mode == ssl.CERT_REQUIRED, 'early_data': h.early_data_accepted})
    for name, opcode, payload in [('text', 1, TEXT), ('binary', 2, BINARY)]:
        client.send(state, frame(opcode, payload))
        observed = await state.next_frame(); expect(observed, opcode, payload)
        cases.append({'case': name, **identity, 'opcode': opcode, 'payload_hex': observed.payload.hex(), 'masked': observed.masked})
    # Split the UTF-8 character across frames; interleave a control frame.
    prefix = 'fragment '.encode() + FRAGMENT[len(b'fragment '):len(b'fragment ')+1]
    client.send(state, frame(1, prefix, fin=False) + frame(9, PING) + frame(0, FRAGMENT[len(prefix):]))
    observed = [await state.next_frame(), await state.next_frame()]
    pong = next(f for f in observed if f.opcode == 10); echoed = next(f for f in observed if f.opcode == 1)
    expect(pong, 10, PING); expect(echoed, 1, FRAGMENT)
    cases.append({'case': 'fragmentation', **identity, 'payload_hex': echoed.payload.hex(), 'pong_hex': pong.payload.hex(), 'masked': False})
    wire = frame(2, BINARY)
    for part in [wire[:1], wire[1:3], wire[3:7], wire[7:]]: client.send(state, part)
    expect(await state.next_frame(), 2, BINARY)
    client.send(state, frame(1, b'coalesced') + frame(2, BINARY))
    expect(await state.next_frame(), 1, b'coalesced'); expect(await state.next_frame(), 2, BINARY)
    cases.append({'case': 'segmentation', **identity, 'split_hex': BINARY.hex(), 'coalesced_hex': [b'coalesced'.hex(), BINARY.hex()]})
    client.send(state, frame(9, PING)); pong = await state.next_frame(); expect(pong, 10, PING)
    client.send(state, frame(1, b'after ping')); echoed = await state.next_frame(); expect(echoed, 1, b'after ping')
    cases.append({'case': 'ping', **identity, 'pong_hex': pong.payload.hex(), 'echo_hex': echoed.payload.hex()})
    cases.append({'case': 'close', **identity, **await clean_close(client, state)})
    invalid, identity = await admit(client, '/ws/echo')
    client.send(invalid, frame(1, b'unmasked', masked=False))
    try:
        await deadline(invalid.terminal)
        raise AssertionError('unmasked tunnel completed cleanly')
    except StreamFailure as failure:
        if not isinstance(failure.event, StreamReset): raise
        termination = {'type': 'StreamReset', 'code': failure.event.error_code}
    witness = await control('wait', event='ws_retired', stream=invalid.stream, connection=client.server_connection)
    health = await deadline(client.request('GET', '/health')['done'])
    healthy = await control('wait', event='health', stream=health['stream'], connection=client.server_connection)
    cases.append({'case': 'invalid_mask', **identity, **termination, 'server': witness, 'sibling_stream': health['stream'], 'sibling_status': response(health)['status'], 'sibling_connection': healthy['connection']})


async def multiplex(client, cases):
    await deadline(client.h3.settings_ready.wait())
    held = client.request('GET', '/hold')
    holding = await control('wait', event='held', stream=held['stream'])
    client.server_connection = holding['connection']
    state, identity = await admit(client, '/ws/echo')
    client._quic.stop_stream(state.stream, 0x10c)
    client._quic.reset_stream(state.stream, 0x10c)
    client.transmit()
    try:
        await deadline(state.terminal); raise AssertionError('reset tunnel succeeded')
    except StreamFailure as failure:
        if not isinstance(failure.event, StreamReset): raise
        code = failure.event.error_code
    reset = await control('wait', event='ws_retired', stream=state.stream, connection=client.server_connection)
    health = await deadline(client.request('GET', '/health')['done'])
    healthy = await control('wait', event='health', stream=health['stream'], connection=client.server_connection)
    cases.append({'case': 'reset', **identity, 'code': code, 'terminal': 'StreamReset', 'server': reset, 'held_stream': held['stream'], 'held_connection': holding['connection'], 'held_pending': not held['done'].done(), 'sibling_stream': health['stream'], 'sibling_connection': healthy['connection'], 'sibling_status': response(health)['status']})
    flood, identity = await admit(client, '/ws/flood', freeze=True)
    until = asyncio.get_running_loop().time() + 5
    while True:
        stream = client._quic._streams[flood.stream]
        if stream.receiver.highest_offset == stream.max_stream_data_local: break
        if asyncio.get_running_loop().time() > until: raise TimeoutError('selected stream did not exhaust receive credit')
        await asyncio.sleep(0.001)
    snapshot = await control('ws_snapshot', connection=identity['connection'], stream=flood.stream)
    health = await deadline(client.request('GET', '/health')['done'])
    healthy = await control('wait', event='health', stream=health['stream'], connection=client.server_connection)
    pending = not held['done'].done()
    client.frozen.remove(flood.stream); client.transmit()
    digest = hashlib.sha256()
    for index in range(64):
        observed = await flood.next_frame()
        expect(observed, 2, bytes([index])*8192); digest.update(observed.payload)
    close = await clean_close(client, flood, initiated=False)
    await control('release', connection=holding['connection'], stream=held['stream'])
    released = await deadline(held['done'])
    cases.append({'case': 'backpressure', **identity, 'credit': 4096, 'offset': 4096, 'snapshot': snapshot, 'held_stream': held['stream'], 'held_connection': holding['connection'], 'held_pending': pending, 'hold_status': response(released)['status'], 'sibling_stream': health['stream'], 'sibling_connection': healthy['connection'], 'sibling_status': response(health)['status'], 'messages': 64, 'body_bytes': 524288, 'body_sha256': digest.hexdigest(), **close})


async def main(args):
    directory = Path(args.log_dir)
    config = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1], server_name='localhost', max_stream_data=4096, max_data=16777216)
    config.load_verify_locations(cafile=args.ca); config.verify_mode = ssl.CERT_REQUIRED
    config.quic_logger = QuicFileLogger(str(directory))
    cases = []
    fd = os.open(directory/'tls.keys', os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, 'w') as keys:
        config.secrets_log_file = keys
        async with connect('127.0.0.1', args.port, configuration=config, create_protocol=WebSocketClient) as client:
            await asyncio.wait_for(echo_cases(client, cases), 12)
        async with connect('127.0.0.1', args.port, configuration=config, create_protocol=WebSocketClient) as client:
            await asyncio.wait_for(multiplex(client, cases), 12)
        wrong = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1], server_name='localhost')
        wrong.load_verify_locations(cafile=args.wrong_ca); wrong.verify_mode = ssl.CERT_REQUIRED
        wrong.quic_logger = config.quic_logger; wrong.secrets_log_file = keys
        attempts = []
        def protocol(*a, **kw):
            p = WebSocketClient(*a, **kw); attempts.append(p); return p
        try:
            async with connect('127.0.0.1', args.port, configuration=wrong, create_protocol=protocol):
                raise AssertionError('wrong CA verified')
        except ConnectionError:
            event = attempts[0].termination
            if not event or event.error_code not in (298, 304): raise AssertionError('missing verification alert')
            cases.append({'case': 'tls_failure', 'type': 'ConnectionTerminated', 'code': event.error_code, 'response': False})
    print(json.dumps({'receipt': {'client': 'aioquic', 'version': aioquic.__version__, 'matrix': 'websocket', 'cases': cases}}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, required=True)
    for arg in ['ca', 'wrong-ca', 'log-dir']: parser.add_argument('--'+arg, required=True)
    asyncio.run(main(parser.parse_args()))
