"""Authenticated public native H3 witness using pinned aioquic."""
import asyncio
import ssl
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integ"))
from http3_websocket_wire import Decoder, frame, expect
from aioquic.h3.connection import FrameType, Setting
from aioquic.asyncio import connect, QuicConnectionProtocol
from aioquic.h3.connection import H3Connection, HeadersState
from aioquic.h3.events import HeadersReceived, DataReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import HandshakeCompleted, ConnectionTerminated, StreamReset


class Client(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.h3 = H3Connection(self._quic)
        original_frame = self.h3._handle_request_or_push_frame
        def response_frame(frame_type, frame_data, stream, stream_ended):
            events=original_frame(frame_type,frame_data,stream,stream_ended)
            for item in events:
                if isinstance(item,HeadersReceived):
                    status=dict(item.headers).get(b':status',b'')
                    if status.startswith(b'1'):
                        assert not stream_ended and status != b'101'
                        # aioquic 1.3.0 treats any second HEADERS as trailers.
                        # Keep its decoder/validation, reopening only 1xx state.
                        stream.headers_recv_state=HeadersState.INITIAL
            return events
        self.h3._handle_request_or_push_frame=response_frame
        self.handshake = None
        self.terminal = None
        self.pending = {}
        self.settings_ready = asyncio.Event()
        original = self.h3._handle_control_frame
        def settings(kind, data):
            original(kind, data)
            if kind == FrameType.SETTINGS: self.settings_ready.set()
        self.h3._handle_control_frame = settings

    def quic_event_received(self, event):
        if isinstance(event, ConnectionTerminated): self.terminal = event
        if isinstance(event, HandshakeCompleted):
            self.handshake = event
        if isinstance(event, (ConnectionTerminated, StreamReset)):
            for stream, state in self.pending.items():
                if isinstance(event, ConnectionTerminated) or event.stream_id == stream:
                    state['terminal'] = event
                    if not state['done'].done():
                        state['done'].set_exception(RuntimeError(str(event)))
        for item in self.h3.handle_event(event):
            state = self.pending.get(getattr(item, 'stream_id', None))
            if state is None:
                continue
            if isinstance(item, HeadersReceived):
                state['sections'].append(item.headers)
                if state.get('opening') and not state['opening'].done(): state['opening'].set_result(item.headers)
            if isinstance(item, DataReceived):
                state['body'].extend(item.data)
                if state.get('decoder'):
                    for observed in state['decoder'].feed(item.data): state['frames'].put_nowait(observed)
            if getattr(item, 'stream_ended', False) and not state['done'].done():
                state['fin_count'] = state.get('fin_count', 0) + 1
                state['done'].set_result(state)

    def request(self, method, path, body=None, trailers=None, end=True, fields=(), websocket=False):
        stream = self._quic.get_next_available_stream_id()
        state = dict(done=asyncio.get_running_loop().create_future(), sections=[], body=bytearray(), stream=stream)
        self.pending[stream] = state
        headers = [(b':method', method.encode()), (b':scheme', b'https'), (b':authority', b'a.example'), (b':path', path.encode())]
        headers.extend(fields)
        if websocket:
            state.update(opening=asyncio.get_running_loop().create_future(), decoder=Decoder(), frames=asyncio.Queue(maxsize=256))
            state['done'].add_done_callback(lambda f: f.exception() if not f.cancelled() else None)
        self.h3.send_headers(stream, headers, end_stream=body is None and end)
        if body is not None:
            for at in range(0, len(body), 3):
                part = body[at:at + 3]
                self.h3.send_data(stream, part, end_stream=end and not trailers and at + len(part) == len(body))
            if trailers:
                self.h3.send_headers(stream, trailers, end_stream=True)
        self.transmit()
        return state


async def basic(port, fixtures):
    config = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1])
    config.server_name = 'a.example'
    config.verify_mode = ssl.CERT_REQUIRED
    config.load_verify_locations(cafile=str(fixtures / 'root.pem'))
    async with connect('127.0.0.1', port, configuration=config, create_protocol=Client) as client:
        handshake = client.handshake
        assert client._quic._retry_count == 1
        assert handshake.alpn_protocol == 'h3' and client._quic._version == 1 and not handshake.early_data_accepted
        result = await asyncio.wait_for(client.request('GET', '/hello')['done'], 5)
        assert result['body'] == b'hello native h3\n', result
        assert (b':status', b'200') in result['sections'][0], result
        assert (b'x-native-h3', b'authenticated-route') in result['sections'][0], result
        assert (b'x-end', b'native-fin') in result['sections'][-1], result
        rows = [dict(case='authenticated_get_trailers', status='passed', stream=result['stream'], body=bytes(result['body']).decode())]
        unknown = await asyncio.wait_for(client.request('GET', '/missing')['done'], 2)
        assert (b':status', b'404') in unknown['sections'][0], unknown
        rows.append(dict(case='unknown_route', status='passed'))
        body = b'incremental installed native h3 upload'
        upload = await asyncio.wait_for(client.request('POST', '/body', body, [(b'x-upload-end', b'observed')])['done'], 2)
        assert upload['body'] == body and (b'x-upload-end', b'observed') in upload['sections'][-1], upload
        rows.append(dict(case='incremental_upload_request_trailers', status='passed', body=bytes(upload['body']).decode()))
        meta = await asyncio.wait_for(client.request('GET', '/meta')['done'], 2)
        assert (b':status', b'204') in meta['sections'][0] and (b'x-protocol', b'h3') in meta['sections'][0] and (b'x-peer', b'127.0.0.1') in meta['sections'][0], meta
        rows.append(dict(case='tls_peer_metadata', status='passed'))
        async def entered(index, minimum):
            for _ in range(100):
                counts = await asyncio.wait_for(client.request('GET', '/counts')['done'], 1)
                values = list(map(int, counts['body'].split()))
                if values[index] >= minimum: return values
            raise AssertionError('public handler entry not observed')
        held = client.request('GET', '/running')
        await entered(2, 1)
        healthy = await asyncio.wait_for(client.request('GET', '/hello')['done'], 1)
        assert healthy['body'] == b'hello native h3\n' and held['stream'] != healthy['stream'] and not held['done'].done()
        await asyncio.wait_for(client.request('GET', '/release')['done'], 1)
        await asyncio.wait_for(held['done'], 1)
        rows.append(dict(case='parallel_running_handler', status='passed', streams=[held['stream'], healthy['stream']]))
        before = await entered(2, 1)
        held = client.request('POST', '/body', b'first', end=False)
        await entered(0, before[0] + 2)
        healthy = await asyncio.wait_for(client.request('GET', '/hello')['done'], 1)
        assert healthy['body'] == b'hello native h3\n' and not held['done'].done()
        client.h3.send_data(held['stream'], b'-last', end_stream=True); client.transmit()
        finished = await asyncio.wait_for(held['done'], 2)
        assert finished['body'] == b'first-last', finished
        rows.append(dict(case='parallel_suspended_upload', status='passed', streams=[held['stream'], healthy['stream']]))
        reset = client.request('POST', '/cancel', b'prefix', end=False)
        await entered(3, 1)
        client._quic.reset_stream(reset['stream'], 0x10c)
        client._quic.stop_stream(reset['stream'], 0x10c); client.transmit()
        try:
            await asyncio.wait_for(reset['done'], 1)
        except RuntimeError:
            pass
        for _ in range(20):
            counts = await asyncio.wait_for(client.request('GET', '/counts')['done'], 1)
            seen, completed, running, cancelled = map(int, counts['body'].split())
            if completed >= seen - 1 and cancelled == 1:
                break
            await asyncio.sleep(.02)
        assert cancelled == 1 and completed == seen - 1, counts
        rows.append(dict(case='reset_stream_local_once', status='passed', cancellation_entries=cancelled))
        for index in range(24):
            recovered = await asyncio.wait_for(client.request('GET', '/hello')['done'], 2)
            assert recovered['body'] == b'hello native h3\n' and (b'x-end', b'native-fin') in recovered['sections'][-1]
        rows.append(dict(case='transport_stream_capacity_recovery', status='passed', requests=24))
        return rows


def configuration(fixtures, server_name='a.example'):
    value = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1], idle_timeout=.6)
    value.server_name = server_name
    value.verify_mode = ssl.CERT_REQUIRED
    value.load_verify_locations(cafile=str(fixtures / 'root.pem'))
    return value


async def rejection(port, fixtures):
    try:
        async with connect('127.0.0.1', port, configuration=configuration(fixtures), create_protocol=Client):
            raise AssertionError('policy accepted handshake')
    except ConnectionError:
        return [dict(case='peer_policy_before_tls', status='passed')]


async def timeout_recovery(port, fixtures):
    async with connect('127.0.0.1', port, configuration=configuration(fixtures), create_protocol=Client) as client:
        await asyncio.wait_for(client.wait_closed(), 2)
    async with connect('127.0.0.1', port, configuration=configuration(fixtures), create_protocol=Client) as client:
        healthy=await asyncio.wait_for(client.request('GET','/hello')['done'], 2)
        assert healthy['body']==b'hello native h3\n'
    return [dict(case='header_timeout_and_recovery',status='passed')]


async def active_stop(port, fixtures, stop):
    async with connect('127.0.0.1', port, configuration=configuration(fixtures), create_protocol=Client) as client:
        held=client.request('POST','/cancel',b'held',end=False)
        await asyncio.sleep(.05)
        await asyncio.to_thread(stop)
        try:
            await asyncio.wait_for(held['done'], 2)
            raise AssertionError('active stop completed body')
        except RuntimeError:
            pass
    return [dict(case='active_stream_server_destruction',status='passed')]


async def failed_handshake(port, fixtures):
    try:
        async with connect('127.0.0.1',port,configuration=configuration(fixtures,'missing.example'),create_protocol=Client):
            raise AssertionError('unknown SNI accepted')
    except ConnectionError:
        pass
    async with connect('127.0.0.1',port,configuration=configuration(fixtures),create_protocol=Client) as client:
        reply=await asyncio.wait_for(client.request('GET','/hello')['done'],2)
        assert reply['body']==b'hello native h3\n'
    return [dict(case='failed_handshake_and_recovery',status='passed')]


async def stalled_handshake(port, fixtures):
    import socket
    import time
    from aioquic.quic.connection import QuicConnection
    endpoint=('127.0.0.1',port)
    peer=QuicConnection(configuration=configuration(fixtures))
    peer.connect(endpoint,now=time.monotonic())
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as stalled:
        stalled.settimeout(1)
        for packet,address in peer.datagrams_to_send(now=time.monotonic()): stalled.sendto(packet,address)
        packet,address=stalled.recvfrom(2048)
        peer.receive_datagram(packet,address,now=time.monotonic())
        assert peer._retry_count==1
        for packet,address in peer.datagrams_to_send(now=time.monotonic()): stalled.sendto(packet,address)
        await asyncio.sleep(4)
        async with connect('127.0.0.1',port,configuration=configuration(fixtures),create_protocol=Client) as client:
            result=await asyncio.wait_for(client.request('GET','/hello')['done'],2)
            assert result['body']==b'hello native h3\n'
    return [dict(case='stalled_handshake_capacity_recovery',status='passed')]


async def websocket_cases(port, fixtures):
    async with connect('127.0.0.1', port, configuration=configuration(fixtures), create_protocol=Client) as client:
        await asyncio.wait_for(client.settings_ready.wait(), 2)
        assert client.h3._received_settings.get(Setting.ENABLE_CONNECT_PROTOCOL) == 1
        tunnel = client.request('CONNECT', '/ws/echo', end=False, websocket=True, fields=[
            (b':protocol', b'websocket'), (b'sec-websocket-version', b'13'),
            (b'sec-websocket-protocol', b'other, harness.v1'), (b'sec-websocket-extensions', b'permessage-deflate')])
        headers = await asyncio.wait_for(tunnel['opening'], 2)
        assert (b':status', b'200') in headers and (b'sec-websocket-protocol', b'harness.v1') in headers, headers
        assert not any(k in (b'upgrade', b'connection', b'sec-websocket-extensions', b'sec-websocket-accept') for k, _ in headers)
        def send(wire, end=False):
            client.h3.send_data(tunnel['stream'], wire, end_stream=end); client.transmit()
        async def observed(): return await asyncio.wait_for(tunnel['frames'].get(), 2)
        for opcode, payload in ((1, b'public text'), (2, b'\x00\xffbinary')):
            send(frame(opcode, payload)); expect(await observed(), opcode, payload)
        fragmented = 'fragment € done'.encode()
        prefix = len(b'fragment ') + 1
        send(frame(1, fragmented[:prefix], fin=False) + frame(9, b'ping') + frame(0, fragmented[prefix:]))
        replies = [await observed(), await observed()]
        expect(next(r for r in replies if r.opcode == 10), 10, b'ping')
        expect(next(r for r in replies if r.opcode == 1), 1, fragmented)
        wire = frame(2, b'segmented')
        for part in (wire[:1], wire[1:3], wire[3:7], wire[7:]): send(part)
        expect(await observed(), 2, b'segmented')
        send(frame(1, b'one') + frame(2, b'two'))
        expect(await observed(), 1, b'one'); expect(await observed(), 2, b'two')
        healthy = await asyncio.wait_for(client.request('GET', '/hello')['done'], 2)
        assert healthy['body'] == b'hello native h3\n'
        send(frame(8, b'\x03\xe8'), end=True)
        expect(await observed(), 8, b'\x03\xe8')
        await asyncio.wait_for(tunnel['done'], 2); tunnel['decoder'].finish()
        return dict(case='rfc9220_wire_corpus', status='passed', text='public text', binary_hex='00ff62696e617279',
                    fragmented_hex=fragmented.hex(), close_code=1000, terminal='FIN', settings_connect=1,
                    subprotocol='harness.v1', sibling_body='hello native h3\n')

async def websocket_refusals(port, fixtures):
    async with connect('127.0.0.1',port,configuration=configuration(fixtures),create_protocol=Client) as client:
        await asyncio.wait_for(client.settings_ready.wait(),2)
        observed=[]
        for mode in ('invalid_mask','message_limit','pending_receive_reset'):
            tunnel=client.request('CONNECT','/ws/echo',end=False,websocket=True,fields=[(b':protocol',b'websocket'),(b'sec-websocket-version',b'13'),(b'sec-websocket-protocol',b'harness.v1')])
            assert (b':status',b'200') in await asyncio.wait_for(tunnel['opening'],2)
            if mode=='pending_receive_reset':
                client._quic.reset_stream(tunnel['stream'],0x10c);client._quic.stop_stream(tunnel['stream'],0x10c)
            else:
                wire=frame(1,b'unmasked',masked=False) if mode=='invalid_mask' else frame(2,b'x'*8192,fin=False)+frame(0,b'y')
                client.h3.send_data(tunnel['stream'],wire,end_stream=False)
            client.transmit()
            try:
                await asyncio.wait_for(tunnel['done'],2)
                raise AssertionError('invalid tunnel completed cleanly')
            except RuntimeError:
                assert isinstance(tunnel['terminal'],StreamReset) and tunnel['terminal'].error_code==0x10c
            healthy=await asyncio.wait_for(client.request('GET','/hello')['done'],2)
            assert healthy['body']==b'hello native h3\n'
            observed.append(mode)
        return dict(case='public_rfc9220_refusals',status='passed',cases=observed,terminal_code=0x10c,sibling_body='hello native h3\n')
