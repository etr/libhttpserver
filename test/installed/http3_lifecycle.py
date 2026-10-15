"""Independent installed aggregate lifecycle predicates, bounded by real events."""

import asyncio
import socket
import ssl
import time
from aioquic.quic.connection import QuicConnection
from aioquic.quic.events import ConnectionTerminated


async def command(child, value):
    child.process.stdin.write((value + "\n").encode())
    child.process.stdin.flush()
    return await asyncio.to_thread(child.line)


async def aggregate(child, fixtures, module, h2_client, expiry):
    m = module
    H2Client = h2_client
    ports = list(map(int, (await asyncio.to_thread(child.line)).split()[1:]))
    assert len(ports) == 4
    context = ssl.create_default_context(cafile=str(fixtures / "root.pem"))
    context.set_alpn_protocols(["http/1.1"])
    h1 = context.wrap_socket(
        socket.create_connection(("127.0.0.1", ports[1]), timeout=3), server_hostname="a.example"
    )
    h2 = H2Client(ports[3], fixtures)
    h1.sendall(b"GET /held HTTP/1.1\r\nHost: a.example\r\n\r\n")
    stream = h2.request("/held")
    config = m.configuration(fixtures)
    config.idle_timeout = 5
    try:
        async with m.connect(
            "127.0.0.1", ports[0], configuration=config, create_protocol=m.Client
        ) as h3, m.connect(
            "127.0.0.1", ports[2], configuration=config, create_protocol=m.Client
        ) as ws:
            request = h3.request("GET", "/held")
            request["done"].add_done_callback(
                lambda f: f.exception() if not f.cancelled() else None
            )
            await asyncio.wait_for(ws.settings_ready.wait(), 2)
            tunnel = ws.request(
                "CONNECT",
                "/ws/echo",
                end=False,
                websocket=True,
                fields=[
                    (b":protocol", b"websocket"),
                    (b"sec-websocket-version", b"13"),
                    (b"sec-websocket-protocol", b"harness.v1"),
                ],
            )
            assert (b":status", b"200") in await asyncio.wait_for(tunnel["opening"], 2)
            for _ in range(100):
                if await command(child, "entries") == "ENTRIES 3":
                    break
            else:
                raise AssertionError("actual held handler entries absent")
            assert (
                await command(child, "begin-expiry" if expiry else "begin-clean") == "DRAIN_BEGUN"
            )
            close = await asyncio.wait_for(tunnel["frames"].get(), 2)
            m.expect(close, 8, b"\x03\xe9")
            if not expiry:
                ws.h3.send_data(tunnel["stream"], m.frame(8, b"\x03\xe9"), end_stream=True)
                ws.transmit()
                assert await command(child, "signal") == "SIGNALED"
                assert (b":status", b"204") in (await asyncio.wait_for(request["done"], 2))[
                    "sections"
                ][0]
                assert b"204" in await asyncio.to_thread(h1.recv, 4096)
                assert (":status", "204") in (await asyncio.to_thread(h2.wait, stream))["headers"]
                await asyncio.wait_for(tunnel["done"], 2)
            result = await command(child, "wait-drain")
            repeat = await command(child, "wait-drain")
            assert repeat == result
            fields = result.split()
            assert fields[1] == ("1" if expiry else "0")
            assert (int(fields[2]) > 0) if expiry else fields[2] == "0"
            if expiry:
                await asyncio.wait_for(h3.wait_closed(), 2)
                assert h3.terminal and h3.terminal.error_code == 0x100, h3.terminal
                assert await command(child, "signal") == "SIGNALED"
            retained = await command(child, "destroy-ticket")
            assert retained == "RETAINED " + " ".join(fields[1:])
            assert await asyncio.to_thread(child.process.wait, 5) == 0
            return dict(
                case="aggregate_deadline" if expiry else "aggregate_clean",
                status="passed",
                listeners=4,
                protocols=["h1", "h2", "h3", "rfc9220"],
                close_code=1001,
                drain_status=int(fields[1]),
                remaining=int(fields[2]),
                stable_wait=True,
                retained_ticket=True,
            )
    finally:
        h1.close()
        h2.close()


async def stalled_tls(child, port, fixtures, module):
    endpoint = ("127.0.0.1", port)
    peer = QuicConnection(configuration=module.configuration(fixtures))
    peer.connect(endpoint, now=time.monotonic())
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as wire:
        wire.settimeout(1)
        for packet, address in peer.datagrams_to_send(now=time.monotonic()):
            wire.sendto(packet, address)
        packet, address = wire.recvfrom(2048)
        peer.receive_datagram(packet, address, now=time.monotonic())
        assert peer._retry_count == 1
        for packet, address in peer.datagrams_to_send(now=time.monotonic()):
            wire.sendto(packet, address)
        packet, address = wire.recvfrom(2048)
        peer.receive_datagram(packet, address, now=time.monotonic())
        result = await command(child, "drain")
        assert result == "DRAIN 1 1", result
        closed = None
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline and closed is None:
            try:
                packet, address = wire.recvfrom(2048)
            except TimeoutError:
                break
            peer.receive_datagram(packet, address, now=time.monotonic())
            if peer._state.name == "DRAINING":
                closed = peer._close_event
            while (event := peer.next_event()) is not None:
                if isinstance(event, ConnectionTerminated):
                    closed = event
        assert closed is not None and closed.error_code == 12, closed
    return dict(
        case="aggregate_stalled_tls_deadline", status="passed", remaining=1, remote_error_code=12
    )


async def handler_drain(port, fixtures, module):
    config = module.configuration(fixtures)
    config.idle_timeout = 5
    async with module.connect(
        "127.0.0.1", port, configuration=config, create_protocol=module.Client
    ) as peer:
        reply = await asyncio.wait_for(peer.request("GET", "/drain-self")["done"], 2)
        assert (b":status", b"204") in reply["sections"][0] and (
            b"x-wait",
            b"would-deadlock",
        ) in reply["sections"][0]
    return dict(case="counted_h3_handler_drain_wait", status="passed", wait="would_deadlock")


async def handler_stop(child, port, fixtures, module):
    config=module.configuration(fixtures);config.idle_timeout=5
    async with module.connect('127.0.0.1',port,configuration=config,create_protocol=module.Client) as peer:
        state=peer.request('GET','/stop-self')
        state['done'].add_done_callback(lambda f:f.exception() if not f.cancelled() else None)
        assert await asyncio.to_thread(child.line)=='HANDLER_STOP_REQUESTED'
        await asyncio.wait_for(peer.wait_closed(),2)
        assert peer.terminal is not None and peer.terminal.error_code==0x100,peer.terminal
    return dict(case='public_h3_handler_request_stop',status='passed',returned_promptly=True,remote_error_code=0x100)


async def migrated_policy(child, port, fixtures, module):
    m = module

    config = m.configuration(fixtures)
    config.idle_timeout = 5
    async with m.connect("127.0.0.1", port, configuration=config, create_protocol=m.Client) as peer:
        old = await asyncio.wait_for(peer.request("GET", "/tls-peer")["done"], 2)
        oldfields = dict(old["sections"][0])
        oldport = int(oldfields[b"x-peer-port"])
        held = peer.request("GET", "/held-meta")
        held["done"].add_done_callback(lambda f: f.exception() if not f.cancelled() else None)
        for _ in range(100):
            if await command(child, "entries") == "ENTRIES 1":
                break
        else:
            raise AssertionError("held exchange did not enter")
        challenges = []
        original, packet_types = peer._quic._QuicConnection__frame_handlers[0x1A]

        def challenge(*args, **kwargs):
            challenges.append(1)
            return original(*args, **kwargs)

        peer._quic._QuicConnection__frame_handlers[0x1A] = (challenge, packet_types)
        previous = peer._transport
        migrated, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            lambda: peer, local_addr=("::", 0), family=socket.AF_INET6
        )
        try:
            newport = migrated.get_extra_info("sockname")[1]
            assert newport != oldport
            await asyncio.wait_for(peer.ping(), 2)
            observed = await asyncio.wait_for(peer.request("GET", "/tls-peer")["done"], 2)
            fields = dict(observed["sections"][0])
            assert challenges and int(fields[b"x-peer-port"]) == newport, fields
            assert fields[b"x-connection"] == oldfields[b"x-connection"]
            assert await command(child, "signal") == "SIGNALED"
            original_exchange = await asyncio.wait_for(held["done"], 2)
            copied = dict(original_exchange["sections"][0])
            assert (
                copied[b"x-peer-port"] == oldfields[b"x-peer-port"]
                and copied[b"x-connection"] == oldfields[b"x-connection"]
            )
            assert await command(child, "reject") == "REJECTING"
            rejected_config = m.configuration(fixtures)
            rejected_config.idle_timeout = 0.6
            before = await command(child, "admissions")
            rejected = []

            def protocol(*args, **kwargs):
                value = m.Client(*args, **kwargs)
                rejected.append(value)
                return value

            try:
                async with m.connect(
                    "127.0.0.1", port, configuration=rejected_config, create_protocol=protocol
                ):
                    raise AssertionError("live policy accepted new connection")
            except ConnectionError:
                pass
            after = await command(child, "admissions")
            b = list(map(int, before.split()[1:]))
            a = list(map(int, after.split()[1:]))
            assert a[1] == b[1] + 1 and a[2:] == b[2:]
            refusal = peer.request("GET", "/hello")
            try:
                await asyncio.wait_for(refusal["done"], 2)
                raise AssertionError("mutated policy dispatched request")
            except RuntimeError:
                assert refusal["terminal"].error_code == 0x10B, refusal["terminal"]
            return dict(
                case="migration_copied_metadata_live_policy",
                status="passed",
                validated_challenges=len(challenges),
                distinct_peer_ports=True,
                copied_peer_preserved=True,
                connection_identity_preserved=True,
                retransmitted_initial_decisions=a[1] - b[1],
                rejected_tls_completions=a[2] - b[2],
                refusal_code=refusal["terminal"].error_code,
            )
        finally:
            peer._transport = previous
            migrated.close()
