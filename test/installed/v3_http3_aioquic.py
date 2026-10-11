"""Authenticated public native H3 witness using pinned aioquic."""
import asyncio
import ssl
from aioquic.asyncio import connect, QuicConnectionProtocol
from aioquic.h3.connection import H3Connection
from aioquic.h3.events import HeadersReceived, DataReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import HandshakeCompleted, ConnectionTerminated, StreamReset


class Client(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.h3 = H3Connection(self._quic)
        self.handshake = None
        self.pending = {}

    def quic_event_received(self, event):
        if isinstance(event, HandshakeCompleted):
            self.handshake = event
        if isinstance(event, (ConnectionTerminated, StreamReset)):
            for stream, state in self.pending.items():
                if isinstance(event, ConnectionTerminated) or event.stream_id == stream:
                    if not state['done'].done():
                        state['done'].set_exception(RuntimeError(str(event)))
        for item in self.h3.handle_event(event):
            state = self.pending.get(getattr(item, 'stream_id', None))
            if state is None:
                continue
            if isinstance(item, HeadersReceived):
                state['sections'].append(item.headers)
            if isinstance(item, DataReceived):
                state['body'].extend(item.data)
            if getattr(item, 'stream_ended', False) and not state['done'].done():
                state['done'].set_result(state)

    def request(self, method, path, body=None, trailers=None, end=True):
        stream = self._quic.get_next_available_stream_id()
        state = dict(done=asyncio.get_running_loop().create_future(), sections=[], body=bytearray(), stream=stream)
        self.pending[stream] = state
        fields = [(b':method', method.encode()), (b':scheme', b'https'), (b':authority', b'a.example'), (b':path', path.encode())]
        self.h3.send_headers(stream, fields, end_stream=body is None and end)
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
        held = client.request('GET', '/running')
        await asyncio.sleep(.05)
        healthy = await asyncio.wait_for(client.request('GET', '/hello')['done'], 1)
        assert healthy['body'] == b'hello native h3\n' and held['stream'] != healthy['stream'] and not held['done'].done()
        await asyncio.wait_for(client.request('GET', '/release')['done'], 1)
        await asyncio.wait_for(held['done'], 1)
        rows.append(dict(case='parallel_running_handler', status='passed', streams=[held['stream'], healthy['stream']]))
        held = client.request('POST', '/body', b'first', end=False)
        await asyncio.sleep(.03)
        healthy = await asyncio.wait_for(client.request('GET', '/hello')['done'], 1)
        assert healthy['body'] == b'hello native h3\n' and not held['done'].done()
        client.h3.send_data(held['stream'], b'-last', end_stream=True); client.transmit()
        finished = await asyncio.wait_for(held['done'], 2)
        assert finished['body'] == b'first-last', finished
        rows.append(dict(case='parallel_suspended_upload', status='passed', streams=[held['stream'], healthy['stream']]))
        reset = client.request('POST', '/cancel', b'prefix', end=False)
        await asyncio.sleep(.05)
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
