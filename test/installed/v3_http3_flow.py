"""Real credit exhaustion and cancellation through installed public body/writer APIs."""

import asyncio
import socket
import time
from resource_limits_probe import measure, validate_window


async def loss_reordering(child, port, fixtures, module, lifecycle, instrumented=False):
    """One-peer relay, with one retained packet and fixed one-shot impairments."""
    loop = asyncio.get_running_loop()
    upstream = ("127.0.0.1", port)

    class Relay(asyncio.DatagramProtocol):
        def __init__(self):
            self.peer = None
            self.armed = False
            self.held = None
            self.timer = None
            self.counts = dict(client_drops=0, server_drops=0, reordered_pairs=0,
                               forwarded_datagrams=0)
            self.received = 0
            self.error = None

        def connection_made(self, transport):
            self.transport = transport

        def forward(self, payload, address):
            self.transport.sendto(payload, address)
            self.counts['forwarded_datagrams'] += 1

        def release(self):
            if self.held is not None:
                self.forward(self.held, self.peer)
                self.held = None
            self.timer = None

        def datagram_received(self, payload, address):
            self.received += 1
            if self.received > 8192 or len(payload) > 2048:
                self.error = 'relay datagram bound'
                return
            server = address == upstream
            if not server:
                if self.peer is None:
                    self.peer = address
                if address != self.peer:
                    self.error = 'relay peer changed'
                    return
            elif self.peer is None:
                self.error = 'server packet before client'
                return
            destination = self.peer if server else upstream
            # Short headers identify protected application packets without
            # decoding, injecting or modifying any protocol state.
            if self.armed and payload and not payload[0] & 0x80:
                key = 'server_drops' if server else 'client_drops'
                if self.counts[key] == 0:
                    self.counts[key] += 1
                    return
                if server and self.counts['reordered_pairs'] == 0:
                    if self.held is None:
                        self.held = payload
                        self.timer = loop.call_later(.05, self.release)
                        return
                    self.forward(payload, destination)
                    self.timer.cancel()
                    self.release()
                    self.counts['reordered_pairs'] += 1
                    return
            self.forward(payload, destination)

        def error_received(self, error):
            self.error = str(error)

    relay = Relay()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: relay, local_addr=('127.0.0.1', 0), family=socket.AF_INET)
    config = module.configuration(fixtures)
    config.idle_timeout = 8
    try:
        async with module.connect('127.0.0.1', transport.get_extra_info('sockname')[1],
                                  configuration=config, create_protocol=module.Client) as peer:
            warm = await asyncio.wait_for(peer.request('GET', '/hello')['done'], 3)
            assert warm['body'] == b'hello native h3\n'
            # Admit the sibling before filling the bounded data/recovery pool.
            # New heads can legitimately be refused once that pool is full.
            sibling = peer.request('GET', '/held-meta')
            for _ in range(100):
                if await lifecycle.command(child, 'entries') == 'ENTRIES 1':
                    break
                await asyncio.wait_for(peer.ping(), 1)
            else:
                raise AssertionError('recovery sibling did not enter its handler')
            relay.armed = True
            large = peer.request('GET', '/large')
            await wait(lambda: bool(large['body']))
            assert await lifecycle.command(child, 'signal') == 'SIGNALED'
            recovery_started = time.monotonic()
            reply, healthy = await asyncio.wait_for(
                asyncio.gather(large['done'], sibling['done']), 12 if instrumented else 6)
            recovery_seconds = time.monotonic() - recovery_started
            assert reply['body'] == bytes(range(256)) * 2048 and reply['fin_count'] == 1
            assert (b':status', b'204') in healthy['sections'][0]
            assert relay.error is None, relay.error
            assert relay.counts['client_drops'] == relay.counts['server_drops'] == 1
            assert relay.counts['reordered_pairs'] == 1, relay.counts
            return dict(case='public_loss_reordering_recovery',status='passed',
                        body_bytes=len(reply['body']),fin_count=reply['fin_count'],
                        sibling_status=204,sibling_admitted_before_pressure=True,
                        instrumented=instrumented,recovery_seconds=recovery_seconds,**relay.counts)
    finally:
        if relay.timer is not None:
            relay.timer.cancel()
        transport.close()


async def websocket_backpressure(child, port, fixtures, module, lifecycle):
    config = module.configuration(fixtures)
    config.idle_timeout = 8
    config.max_stream_data = 4096
    config.max_data = 16777216

    class Frozen(module.Client):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.frozen = set()
            original = self._quic._write_stream_limits

            def limits(builder, space, stream):
                if stream.stream_id not in self.frozen:
                    original(builder, space, stream)

            self._quic._write_stream_limits = limits

    async with module.connect('127.0.0.1', port, configuration=config,
                              create_protocol=Frozen) as peer:
        await asyncio.wait_for(peer.settings_ready.wait(), 2)
        messages = 0
        for cycle in range(2):
            tunnel = peer.request('CONNECT', '/ws/echo', end=False, websocket=True, fields=[
                (b':protocol', b'websocket'), (b'sec-websocket-version', b'13'),
                (b'sec-websocket-protocol', b'harness.v1')])
            peer.frozen.add(tunnel['stream'])
            assert (b':status', b'200') in await asyncio.wait_for(tunnel['opening'], 2)
            payloads = []
            for index in range(16):
                payload = bytes([index]) * 4096
                payloads.append(payload)
                peer.h3.send_data(tunnel['stream'], module.frame(2, payload), end_stream=False)
                peer.transmit()
                await asyncio.wait_for(peer.ping(), 2)
                if await lifecycle.command(child, 'ws-state') == f'WS 1 {cycle}':
                    break
            else:
                raise AssertionError('public WebSocket writable did not become pending')
            stream = peer._quic._streams[tunnel['stream']]
            await wait(lambda: stream.receiver.highest_offset == stream.max_stream_data_local)
            healthy = await asyncio.wait_for(peer.request('GET', '/hello')['done'], 2)
            assert healthy['body'] == b'hello native h3\n' and not tunnel['done'].done()
            if cycle == 0:
                peer.frozen.remove(tunnel['stream'])
                peer.transmit()
                for payload in payloads:
                    module.expect(await asyncio.wait_for(tunnel['frames'].get(), 3), 2, payload)
                messages = len(payloads)
                peer.h3.send_data(tunnel['stream'], module.frame(8, b'\x03\xe8'), end_stream=True)
                peer.transmit()
                module.expect(await asyncio.wait_for(tunnel['frames'].get(), 2), 8, b'\x03\xe8')
                await asyncio.wait_for(tunnel['done'], 2)
                tunnel['decoder'].finish()
            else:
                peer._quic.reset_stream(tunnel['stream'], 0x10c)
                peer.transmit()
                # Acknowledge receive RESET and retire the handler before
                # STOP_SENDING: the two QUIC halves remain independent.
                await state_until(child, 'ws-state', f'WS 0 {cycle+1}', peer, lifecycle)
                peer._quic.stop_stream(tunnel['stream'], 0x10c)
                peer.transmit()
                try:
                    await asyncio.wait_for(tunnel['done'], 2)
                    raise AssertionError('cancelled pending writable completed cleanly')
                except RuntimeError:
                    assert tunnel['terminal'].error_code == 0x10c
            for _ in range(100):
                if await lifecycle.command(child, 'ws-state') == f'WS 0 {cycle+1}':
                    break
                await asyncio.wait_for(peer.ping(), 1)
            else:
                raise AssertionError('WebSocket handler/writable did not retire')
        return dict(case='public_rfc9220_writable_backpressure',status='passed',
                    withheld_credit=4096,resumed_messages=messages,cancelled_pending_writable=1,
                    handler_returns=2,terminal_code=0x10c,sibling_body='hello native h3\n',
                    reset_before_stop_sending=True)


async def lease_cancellation(child, port, fixtures, module, lifecycle):
    config = module.configuration(fixtures)
    config.idle_timeout = 8
    config.max_stream_data = 4096

    class Frozen(module.Client):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.frozen = set()
            original = self._quic._write_stream_limits

            def limits(builder, space, stream):
                if stream.stream_id not in self.frozen:
                    original(builder, space, stream)

            self._quic._write_stream_limits = limits

    async with module.connect('127.0.0.1', port, configuration=config,
                              create_protocol=Frozen) as peer:
        for index, path in enumerate(('/borrowed-large', '/owned-file-large')):
            response = peer.request('GET', path)
            peer.frozen.add(response['stream'])
            stream = peer._quic._streams[response['stream']]
            await wait(lambda: stream.receiver.highest_offset == stream.max_stream_data_local)
            expected = 'LEASES 1 0 0 1' if index == 0 else 'LEASES 0 0 1 1'
            assert await lifecycle.command(child, 'lease-state') == expected
            healthy = await asyncio.wait_for(peer.request('GET', '/hello')['done'], 2)
            assert healthy['body'] == b'hello native h3\n'
            peer._quic.stop_stream(response['stream'], 0x10c)
            peer._quic.reset_stream(response['stream'], 0x10c)
            peer.transmit()
            try:
                await asyncio.wait_for(response['done'], 2)
                raise AssertionError('partial leased response completed cleanly')
            except RuntimeError:
                assert response['terminal'].error_code == 0x10c
            for _ in range(100):
                if await lifecycle.command(child, 'lease-state') == f'LEASES 0 {index} {index+1} 0':
                    break
                await asyncio.wait_for(peer.ping(), 1)
            else:
                raise AssertionError('partial response retained its lease or handler')
        return dict(case='public_partial_response_lease_cancellation', status='passed',
                    withheld_credit=4096, borrowed_live=0, file_closes=1,
                    handler_returns=2, pending_handlers=0, terminal_code=0x10c)


async def multipart_cancellation(port, fixtures, module):
    assert not list(fixtures.glob('lht-upload-*')), 'previous upload retained temporary files'
    async with module.connect('127.0.0.1', port, configuration=module.configuration(fixtures),
                              create_protocol=module.Client) as peer:
        partial = (b'--boundary\r\nContent-Disposition: form-data; name="upload"; '
                   b'filename="cancel.bin"\r\n\r\n' + b'x' * 1024)
        upload = peer.request('POST', '/multipart', partial, end=False,
                              fields=[(b'content-type', b'multipart/form-data; boundary=boundary')])
        await wait(lambda: len(list(fixtures.glob('lht-upload-*'))) == 1)
        healthy = await asyncio.wait_for(peer.request('GET', '/hello')['done'], 2)
        assert healthy['body'] == b'hello native h3\n' and not upload['done'].done()
        peer._quic.stop_stream(upload['stream'], 0x10c)
        peer._quic.reset_stream(upload['stream'], 0x10c)
        peer.transmit()
        try:
            await asyncio.wait_for(upload['done'], 2)
            raise AssertionError('cancelled partial multipart completed cleanly')
        except RuntimeError:
            assert upload['terminal'].error_code == 0x10c
        await wait(lambda: not list(fixtures.glob('lht-upload-*')))
        return dict(case='public_multipart_cancellation_cleanup', status='passed',
                    partial_files=1, retained_files=0, terminal_code=0x10c)


def sample_window(pid, before, after, start, end, normal_allocator_only):
    sample = dict(
        server_pid=pid,
        cpu_seconds=after[0] - before[0],
        wall_seconds=end - start,
        rss_before=before[1],
        rss_after=after[1],
    )
    if normal_allocator_only:
        validate_window(sample)
    else:
        assert (
            sample["wall_seconds"] >= 0.99 and sample["cpu_seconds"] / sample["wall_seconds"] <= 0.2
        ), sample
    return sample


async def wait(predicate):
    deadline = time.monotonic() + 3
    while not predicate():
        assert time.monotonic() < deadline, "actual transport window did not exhaust"
        await asyncio.sleep(0.001)


async def state_until(child, value, expected, peer, lifecycle):
    deadline = time.monotonic() + 3
    while await lifecycle.command(child, value) != expected:
        remaining = deadline - time.monotonic()
        assert remaining > 0, 'public handler state did not reach ' + expected
        await asyncio.wait_for(peer.ping(), min(1, remaining))


async def saturation(child, port, fixtures, module, lifecycle, normal_allocator_only=True):
    m = module
    life = lifecycle
    config = m.configuration(fixtures)
    config.idle_timeout = 5
    config.max_stream_data = 4096
    config.max_data = 16777216

    class Frozen(m.Client):
        def __init__(self, *a, **kw):
            super().__init__(*a, **kw)
            self.frozen = set()
            original = self._quic._write_stream_limits

            def limits(builder, space, stream):
                if stream.stream_id not in self.frozen:
                    original(builder, space, stream)

            self._quic._write_stream_limits = limits

    async with m.connect("127.0.0.1", port, configuration=config, create_protocol=Frozen) as peer:

        async def hello():
            result = await asyncio.wait_for(peer.request("GET", "/hello")["done"], 2)
            assert result["body"] == b"hello native h3\n"

        # This is setup transfer throughput, separate from the fixed one-second
        # stalled CPU/RSS gate below. TSan's real byte/FIN probes took 4–5s
        # during rebuilds; keep a measured, bounded instrumentation allowance.
        warm_started = time.monotonic()
        warm = await asyncio.wait_for(peer.request("GET", "/large")["done"],
                                      3 if normal_allocator_only else 8)
        warm_seconds = time.monotonic() - warm_started
        assert len(warm["body"]) == 524288 and warm["fin_count"] == 1
        await hello()
        baseline = measure(child.process.pid)[1]
        samples = []
        for cycle in range(3):
            state = peer.request("GET", "/large")
            peer.frozen.add(state["stream"])
            stream = peer._quic._streams[state["stream"]]
            await wait(lambda: stream.receiver.highest_offset == stream.max_stream_data_local)
            assert await life.command(child, "flow-state") == f"FLOW {cycle} 1"
            await hello()
            before = measure(child.process.pid)
            start = time.monotonic()
            await asyncio.sleep(1)
            sample = sample_window(
                child.process.pid,
                before,
                measure(child.process.pid),
                start,
                time.monotonic(),
                normal_allocator_only,
            )
            assert (
                not state["done"].done()
                and stream.receiver.highest_offset == stream.max_stream_data_local
            )
            for _ in range(64):
                await asyncio.wait_for(peer.ping(), 1)
            peer._quic.stop_stream(state["stream"], 0x10C)
            peer._quic.reset_stream(state["stream"], 0x10C)
            peer.transmit()
            try:
                await asyncio.wait_for(state["done"], 2)
                raise AssertionError("reset download completed cleanly")
            except RuntimeError:
                assert state["terminal"].error_code == 0x10C, state["terminal"]
            for _ in range(100):
                if await life.command(child, "flow-state") == f"FLOW {cycle} 0":
                    break
            else:
                raise AssertionError("cancelled writer retained")
            await hello()
            assert not normal_allocator_only or measure(child.process.pid)[1] <= baseline + 1048576
            upload = peer.request(
                "POST", "/upload-hold", end=False, fields=[(b"content-length", b"131072")]
            )
            payload = bytes(range(256)) * 512
            peer.h3.send_data(upload["stream"], payload, end_stream=True)
            peer.transmit()
            out = peer._quic._streams[upload["stream"]]
            await wait(
                lambda: out.sender.highest_offset == out.max_stream_data_remote
                and out.sender.highest_offset < out.sender._buffer_stop
            )
            await state_until(child, "flow-state", f"FLOW {cycle+1} 0", peer, life)
            await hello()
            before = measure(child.process.pid)
            start = time.monotonic()
            await asyncio.sleep(1)
            incoming_sample = sample_window(
                child.process.pid,
                before,
                measure(child.process.pid),
                start,
                time.monotonic(),
                normal_allocator_only,
            )
            assert (
                not upload["done"].done()
                and out.sender.highest_offset == out.max_stream_data_remote
            )
            assert await life.command(child, "resume-upload") == f"UPLOAD_RESUMED {cycle+1}"
            reply = await asyncio.wait_for(upload["done"], 3)
            assert reply["body"] == payload and reply["fin_count"] == 1
            samples.extend([sample, incoming_sample])
        return dict(
            case="public_flow_saturation_recovery",
            status="passed",
            cycles=3,
            resumed_bytes=len(reply["body"]),
            download_bytes=len(warm["body"]),
            ping_acks=192,
            terminal_code=0x10C,
            normal_allocator_only=normal_allocator_only,
            warm_seconds=warm_seconds,
            windows=samples,
        )
