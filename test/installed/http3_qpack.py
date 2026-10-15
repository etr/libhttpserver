"""Independent blocked-QPACK wire sections against the installed native listener."""

import asyncio
import pylsqpack
from aioquic.h3.connection import FrameType, Setting, encode_frame


async def unblock(child, port, fixtures, module):
    m = module
    config = m.configuration(fixtures)
    config.idle_timeout = 5
    async with m.connect("127.0.0.1", port, configuration=config, create_protocol=m.Client) as peer:
        await asyncio.wait_for(peer.settings_ready.wait(), 2)
        assert (
            peer.h3._received_settings[Setting.QPACK_MAX_TABLE_CAPACITY] == 4096
            and peer.h3._received_settings[Setting.QPACK_BLOCKED_STREAMS] == 8
        )
        field = [(b"x-qpack-public", b"value-" + b"z" * 160)]
        warm = await asyncio.wait_for(peer.request("GET", "/hello", fields=field)["done"], 2)
        assert warm["body"] == b"hello native h3\n"
        saved = []
        original = peer._quic.send_stream_data

        def send(stream, data, end_stream=False):
            if stream == peer.h3._local_encoder_stream_id:
                saved.append(bytes(data))
                assert sum(map(len, saved)) <= 8192
            else:
                original(stream, data, end_stream=end_stream)

        peer._quic.send_stream_data = send
        blocked = peer.request("GET", "/hello", fields=field)
        assert any(saved), "encoder made no actual blocked section"
        stream = peer._quic.get_next_available_stream_id()
        state = dict(
            done=asyncio.get_running_loop().create_future(),
            sections=[],
            body=bytearray(),
            stream=stream,
        )
        peer.pending[stream] = state
        literal = pylsqpack.Encoder()
        _, section = literal.encode(
            stream,
            [
                (b":method", b"GET"),
                (b":scheme", b"https"),
                (b":authority", b"a.example"),
                (b":path", b"/hello"),
            ],
        )
        original(stream, encode_frame(FrameType.HEADERS, section), end_stream=True)
        peer.transmit()
        result = await asyncio.wait_for(state["done"], 2)
        assert result["body"] == b"hello native h3\n" and not blocked["done"].done()
        peer._quic.send_stream_data = original
        for data in saved:
            original(peer.h3._local_encoder_stream_id, data)
        peer.transmit()
        reply = await asyncio.wait_for(blocked["done"], 2)
        assert reply["body"] == b"hello native h3\n"
        return dict(
            case="public_qpack_blocked_unblock",
            status="passed",
            table_capacity=4096,
            blocked_limit=8,
            encoder_bytes=sum(map(len, saved)),
            sibling_before_release=True,
            body=bytes(reply["body"]).decode(),
        )


async def terminal_case(child, port, fixtures, module, lifecycle, mode):
    config = module.configuration(fixtures)
    config.idle_timeout = 5
    async with module.connect(
        "127.0.0.1", port, configuration=config, create_protocol=module.Client
    ) as peer:
        await asyncio.wait_for(peer.settings_ready.wait(), 2)
        field = [(b"x-upload-end", b"public accepted trailer")]
        warm = await asyncio.wait_for(peer.request("GET", "/hello", fields=field)["done"], 2)
        assert warm["body"] == b"hello native h3\n"
        if mode == "trailers":
            held = peer.request(
                "POST", "/semantic", b"accepted", end=False, fields=[(b"content-length", b"8")]
            )
            counted = await asyncio.wait_for(peer.request("GET", "/counts")["done"], 2)
            assert int(counted["body"].split()[0]) == 3
        saved = []
        original = peer._quic.send_stream_data

        def send(stream, data, end_stream=False):
            if stream == peer.h3._local_encoder_stream_id:
                saved.append(bytes(data))
                assert sum(map(len, saved)) <= 8192
            else:
                original(stream, data, end_stream=end_stream)

        peer._quic.send_stream_data = send
        if mode == "trailers":
            peer.h3.send_headers(held["stream"], field, end_stream=True)
            peer.transmit()
        else:
            held = peer.request("GET", "/hello", fields=field)
        held["done"].add_done_callback(lambda f: f.exception() if not f.cancelled() else None)
        assert any(saved), "no genuinely withheld dynamic section"
        stream = peer._quic.get_next_available_stream_id()
        sibling = dict(
            done=asyncio.get_running_loop().create_future(),
            sections=[],
            body=bytearray(),
            stream=stream,
        )
        peer.pending[stream] = sibling
        _, section = pylsqpack.Encoder().encode(
            stream,
            [
                (b":method", b"GET"),
                (b":scheme", b"https"),
                (b":authority", b"a.example"),
                (b":path", b"/hello"),
            ],
        )
        original(stream, encode_frame(FrameType.HEADERS, section), end_stream=True)
        peer.transmit()
        healthy = await asyncio.wait_for(sibling["done"], 2)
        assert healthy["body"] == b"hello native h3\n" and not held["done"].done()
        if mode == "reset":
            peer._quic.reset_stream(held["stream"], 0x10C)
            peer._quic.stop_stream(held["stream"], 0x10C)
            peer.transmit()
        else:
            assert await lifecycle.command(child, "begin-clean") == "DRAIN_BEGUN"
        if mode != "trailers":
            try:
                await asyncio.wait_for(held["done"], 2)
                raise AssertionError("blocked initial completed")
            except RuntimeError:
                assert held["terminal"].error_code == (0x10C if mode == "reset" else 0x10B), held[
                    "terminal"
                ]
        peer._quic.send_stream_data = original
        for data in saved:
            original(peer.h3._local_encoder_stream_id, data)
        peer.transmit()
        if mode == "trailers":
            reply = await asyncio.wait_for(held["done"], 2)
            assert (
                reply["body"] == b"accepted"
                and (b"x-upload-end", b"public accepted trailer") in reply["sections"][-1]
                and reply["fin_count"] == 1
            )
        if mode != "reset":
            assert await lifecycle.command(child, "wait-drain") == "DRAIN_RESULT 0 0"
        return dict(
            case="public_qpack_" + mode,
            status="passed",
            encoder_bytes=sum(map(len, saved)),
            sibling_before_release=True,
            terminal_code=0x10C if mode == "reset" else 0x10B if mode == "initial" else 0,
            accepted_trailer_completed=mode == "trailers",
        )
