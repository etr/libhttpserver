"""Pinned aioquic test adapter for incremental H3 and receive-credit witnesses."""
import asyncio
import hashlib
from aioquic.buffer import Buffer
from aioquic.asyncio import QuicConnectionProtocol
from aioquic.h3.connection import H3Connection, FrameType
from aioquic.h3.events import HeadersReceived, DataReceived
from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted, StreamReset
from http3_client_aioquic import Client, StreamFailure, UPLOAD, control, response


class ObservedH3(H3Connection):
    def __init__(self, quic):
        super().__init__(quic)
        self.goaway = []

    def _handle_control_frame(self, frame_type, frame_data):
        super()._handle_control_frame(frame_type, frame_data)
        if frame_type == FrameType.GOAWAY:
            buffer = Buffer(data=frame_data)
            value = buffer.pull_uint_var()
            if not buffer.eof():
                raise RuntimeError('malformed GOAWAY')
            self.goaway.append(value)


class SemanticClient(Client):
    def __init__(self, *args, h3_factory=ObservedH3, **kwargs):
        QuicConnectionProtocol.__init__(self, *args, **kwargs)
        self.pending = {}
        self.handshake = None
        self.termination = None
        self.h3 = h3_factory(self._quic)
        self.frozen = set()
        original = self._quic._write_stream_limits

        def stream_limits(builder, space, stream):
            # aioquic 1.3.0 normally grows credit from received offsets, before consumption.
            if stream.stream_id not in self.frozen:
                original(builder, space, stream)
        self._quic._write_stream_limits = stream_limits

    def quic_event_received(self, event):
        if isinstance(event, HandshakeCompleted):
            self.handshake = event
        if isinstance(event, ConnectionTerminated):
            self.termination = event
        if isinstance(event, (StreamReset, ConnectionTerminated)):
            for stream, state in list(self.pending.items()):
                if isinstance(event, ConnectionTerminated) or stream == event.stream_id:
                    if not state['done'].done():
                        state['done'].set_exception(StreamFailure(event))
                    self.pending.pop(stream)
        for item in self.h3.handle_event(event):
            state = self.pending.get(getattr(item, 'stream_id', None))
            if state is None:
                continue
            state.setdefault('trailers', [])
            state.setdefault('fin_count', 0)
            if isinstance(item, HeadersReceived):
                state['trailers' if state['headers'] else 'headers'].extend(item.headers)
            elif isinstance(item, DataReceived):
                state['body'].extend(item.data)
                if len(state['body']) > 524288:
                    raise RuntimeError('response body bound exceeded')
            if item.stream_ended:
                state['fin_count'] += 1
                state['done'].set_result(state)
                self.pending.pop(item.stream_id)

    def start(self, method, path, length=0, finish=False, extra=()):
        stream = self._quic.get_next_available_stream_id()
        state = {'done': asyncio.get_running_loop().create_future(), 'headers': [], 'trailers': [], 'body': bytearray(), 'stream': stream, 'fin_count': 0}
        self.pending[stream] = state
        fields = [(b':method', method.encode()), (b':scheme', b'https'), (b':authority', b'localhost'), (b':path', path.encode()), *extra]
        if length:
            fields.append((b'content-length', str(length).encode()))
        self.h3.send_headers(stream, fields, end_stream=finish)
        self.transmit()
        return state


def fields(values):
    return [[key.decode(), value.decode()] for key, value in values]


async def done(state):
    return await asyncio.wait_for(state['done'], 5)


async def uploads(client, cases):
    for name in ['streaming', 'slow_producer']:
        state = client.start('POST', '/items/../semantic?x=1', len(UPLOAD), extra=[(b'x-repeat', b'a'), (b'x-repeat', b'b')])
        client.h3.send_data(state['stream'], UPLOAD[:6], end_stream=False)
        client.transmit()
        observed = await control('wait', event='read', stream=state['stream'], min_bytes=6)
        c = {'case': name, 'connection': observed['connection'], 'prefix_bytes': observed['read_bytes'], 'before_fin': not state['done'].done()}
        if name == 'slow_producer':
            sibling = await done(client.request('GET', '/health'))
            witness = await control('wait', event='health', stream=sibling['stream'])
            c.update(sibling_stream=sibling['stream'], sibling_status=response(sibling)['status'], sibling_connection=witness['connection'])
        client.h3.send_data(state['stream'], UPLOAD[6:], end_stream=False)
        client.h3.send_headers(state['stream'], [(b'x-end', b'yes')], end_stream=True)
        client.transmit()
        result = await done(state)
        c.update(response(result), headers=fields(result['headers']), trailers=fields(result['trailers']), fin_count=result['fin_count'])
        cases.append(c)
    state = client.start('GET', '/large', finish=True)
    client.frozen.add(state['stream'])
    # Wait for the selected stream to reach its finite advertised credit; ACKs remain live.
    deadline = asyncio.get_running_loop().time() + 5
    while True:
        stream = client._quic._streams[state['stream']]
        if stream.receiver.highest_offset == stream.max_stream_data_local:
            break
        if asyncio.get_running_loop().time() > deadline:
            raise RuntimeError(f'stream never exhausted receive credit: {stream.receiver.highest_offset}/{stream.max_stream_data_local}')
        await asyncio.sleep(0.001)
    credit, offset = stream.max_stream_data_local, stream.receiver.highest_offset
    sibling = await done(client.request('GET', '/health'))
    witness = await control('wait', event='health', stream=sibling['stream'])
    snapshot = await control('snapshot', connection=witness['connection'], stream=state['stream'])
    client.frozen.remove(state['stream'])
    client.transmit()
    result = await done(state)
    cases.append({'case': 'slow_consumer', 'connection': snapshot['connection'], 'stream': state['stream'], 'sibling_stream': sibling['stream'], 'sibling_connection': witness['connection'], 'sibling_status': response(sibling)['status'], 'credit': credit, 'offset': offset, 'withheld': True, 'started': snapshot['started'], 'completed': snapshot['completed'], 'resumed': True, 'status': response(result)['status'], 'body_bytes': len(result['body']), 'body_sha256': hashlib.sha256(result['body']).hexdigest(), 'trailers': fields(result['trailers']), 'fin_count': result['fin_count']})


async def draining(client, cases):
    held = client.request('GET', '/hold')
    observed = await control('wait', event='held', stream=held['stream'])
    # Reserve the late stream before quic-go's equivalent refuses new opens on GOAWAY.
    late = client._quic.get_next_available_stream_id()
    await control('drain', connection=observed['connection'], stream=held['stream'])
    deadline = asyncio.get_running_loop().time() + 5
    while len(client.h3.goaway) < 2:
        if asyncio.get_running_loop().time() > deadline:
            raise RuntimeError('missing received GOAWAY stages')
        await asyncio.sleep(0.001)
    rejected = client.start('GET', '/hello', finish=True)
    assert rejected['stream'] == late
    try:
        await done(rejected)
        raise AssertionError('late request accepted')
    except StreamFailure as failure:
        assert isinstance(failure.event, StreamReset)
        code = failure.event.error_code
    await control('release', connection=observed['connection'], stream=held['stream'])
    result = await done(held)
    await control('wait', event='drained', stream=0, connection=observed['connection'])
    await asyncio.wait_for(client.wait_closed(), 5)
    cases.append({'case': 'goaway', 'connection': observed['connection'], 'accepted_stream': held['stream'], 'late_stream': late, 'ids': client.h3.goaway, 'rejection': code, 'accepted_status': response(result)['status'], 'close_code': client.termination.error_code, 'drain_complete': True})
