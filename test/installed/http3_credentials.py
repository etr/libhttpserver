"""Verified public QUIC credentials and genuine independent early-data attempts."""

import asyncio
import dataclasses
import hashlib
import ssl
from cryptography.hazmat.primitives.serialization import Encoding
from aioquic.buffer import Buffer
from aioquic.quic.packet import pull_quic_header, QuicPacketType


class HeldHandshake:
    """Hold the real client Finished, after the selected certificate arrives."""
    @staticmethod
    def protocol(module):
        class Peer(module.Client):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                self.held = []
                self.holding = True
                self.blocked = asyncio.Event()

            def connection_made(self, transport):
                super().connection_made(transport)
                self.send = transport.sendto

                def send(payload, address=None):
                    if self.holding and self._quic.tls.state.name == 'CLIENT_POST_HANDSHAKE':
                        assert sum(len(p) for p, _ in self.held) + len(payload) <= 32768
                        self.held.append((payload, address))
                        self.blocked.set()
                    else:
                        self.send(payload, address)

                transport.sendto = send

            def release(self):
                self.holding = False
                for payload, address in self.held:
                    self.send(payload, address)
                self.held.clear()
        return Peer


async def command(child, value, expected):
    child.process.stdin.write((value + "\n").encode())
    child.process.stdin.flush()
    assert await asyncio.to_thread(child.line) == expected


async def credentials(port, fixtures, module, child):
    rows = []
    digest = lambda name: hashlib.sha256(
        ssl.PEM_cert_to_DER_cert(
            (fixtures / (name + ".pem")).read_text().split("-----END CERTIFICATE-----")[0]
            + "-----END CERTIFICATE-----\n"
        )
    ).hexdigest()

    def config(name="a.example", verify_name=True):
        value = module.configuration(fixtures, name)
        if not verify_name:
            value.verify_mode = ssl.CERT_NONE
        value.idle_timeout = 5
        return value

    async def hello(peer):
        result = await asyncio.wait_for(peer.request("GET", "/hello")["done"], 3)
        assert result["body"] == b"hello native h3\n"

    tickets = []
    async with module.connect(
        "127.0.0.1",
        port,
        configuration=config(),
        create_protocol=module.Client,
        session_ticket_handler=tickets.append,
    ) as established:
        await hello(established)
        assert hashlib.sha256(
            established._quic.tls._peer_certificate.public_bytes(Encoding.DER)
        ).hexdigest() == digest("a")
        for name, expected, verified in [
            ("b.example", "b", True),
            ("A.EXAMPLE.", "a", False),
            ("missing.example", "a", False),
            (None, "a", False),
        ]:
            async with module.connect(
                "127.0.0.1",
                port,
                configuration=config(name, verified),
                create_protocol=module.Client,
            ) as peer:
                await hello(peer)
                observed = hashlib.sha256(
                    peer._quic.tls._peer_certificate.public_bytes(Encoding.DER)
                ).hexdigest()
                assert observed == digest(expected)
        rows.append(dict(case="sni_default_canonical", status="passed", hosts=2))
        assert tickets, "server emitted no session ticket"
        resumed_config = config()
        resumed_config.session_ticket = tickets[0]
        async with module.connect(
            "127.0.0.1", port, configuration=resumed_config, create_protocol=module.Client
        ) as resumed:
            await hello(resumed)
            assert resumed.handshake.session_resumed and not resumed.handshake.early_data_accepted
        rows.append(dict(case="session_resumption", status="passed", resumed=True))
        before = await asyncio.wait_for(established.request("GET", "/counts")["done"], 3)
        seen_before = int(before["body"].split()[0])
        early_config = config()
        early_config.session_ticket = dataclasses.replace(
            tickets[0], max_early_data_size=0xFFFFFFFF
        )
        packets = []
        async with module.connect(
            "127.0.0.1",
            port,
            configuration=early_config,
            create_protocol=module.Client,
            wait_connected=False,
        ) as early:
            original = early._quic.datagrams_to_send

            def datagrams(now):
                values = original(now)
                for payload, _ in values:
                    buffer = Buffer(data=payload)
                    while not buffer.eof():
                        start = buffer.tell()
                        if not any(payload[start:]):
                            break  # Initial datagram padding outside retained packets.
                        header = pull_quic_header(buffer, host_cid_length=8)
                        if header.packet_type == QuicPacketType.ZERO_RTT:
                            packets.append(header.packet_length)
                        buffer.seek(start + header.packet_length)
                return values

            early._quic.datagrams_to_send = datagrams
            request = early.request("GET", "/hello")
            request["done"].add_done_callback(
                lambda f: f.exception() if not f.cancelled() else None
            )
            await asyncio.wait_for(early.wait_connected(), 3)
            assert packets and not early.handshake.early_data_accepted
            after = await asyncio.wait_for(established.request("GET", "/counts")["done"], 3)
            assert int(after["body"].split()[0]) == seen_before + 1, "early handler entered"
        async with module.connect(
            "127.0.0.1", port, configuration=config(), create_protocol=module.Client
        ) as ordinary:
            await hello(ordinary)
        rows.append(
            dict(
                case="zero_rtt_attempt_rejected",
                status="passed",
                attempted_packets=len(packets),
                early_data_accepted=False,
                early_handler_entries=0,
            )
        )
        await command(child, "acme", "ACME_PUBLISHED")
        async with module.connect(
            "127.0.0.1", port, configuration=config(), create_protocol=module.Client
        ) as normal:
            await hello(normal)
            assert hashlib.sha256(
                normal._quic.tls._peer_certificate.public_bytes(Encoding.DER)
            ).hexdigest() == digest("a")
        challenge = config()
        challenge.alpn_protocols = ["acme-tls/1"]
        try:
            async with module.connect(
                "127.0.0.1", port, configuration=challenge, create_protocol=module.Client
            ):
                raise AssertionError("QUIC selected challenge")
        except ConnectionError:
            pass
        await command(child, "remove-acme", "ACME_REMOVED")
        rows.append(dict(case="acme_quic_isolation", status="passed", challenge_alpn_rejected=True))
        await command(child, "invalid-replace", "PRESERVED")
        await hello(established)
        async with module.connect("127.0.0.1",port,configuration=config(),
                                  create_protocol=HeldHandshake.protocol(module),wait_connected=False) as held:
            held.transmit()
            await asyncio.wait_for(held.wait_connected(),3)
            await asyncio.wait_for(held.blocked.wait(),2)
            held_bytes = sum(len(p) for p, _ in held.held)
            assert held_bytes > 0
            assert hashlib.sha256(held._quic.tls._peer_certificate.public_bytes(Encoding.DER)).hexdigest() == digest('a')
            await command(child, "rotate", "ROTATED")
            held.release()
            await hello(held)
            rows.append(dict(case='credential_rotation_held_handshake',status='passed',
                             held_finished_bytes=held_bytes,selected_old_certificate=True))
        await hello(established)
        rotated = config()
        rotated.session_ticket = tickets[0]
        async with module.connect(
            "127.0.0.1", port, configuration=rotated, create_protocol=module.Client
        ) as fresh:
            await hello(fresh)
            assert not fresh.handshake.session_resumed
            assert hashlib.sha256(
                fresh._quic.tls._peer_certificate.public_bytes(Encoding.DER)
            ).hexdigest() == digest("a-replacement")
        rows.append(
            dict(
                case="credential_rotation_existing_new",
                status="passed",
                old_session_resumed=False,
                established_requests=3,
            )
        )
    return rows


async def mtls(port, fixtures, module, required):
    rows = []
    for identity in (None, "client-valid", "client-other"):
        config = module.configuration(fixtures)
        config.idle_timeout = 2
        if identity:
            config.load_cert_chain(
                str(fixtures / (identity + "-chain.pem")), str(fixtures / (identity + "-key.pem"))
            )
        rejected = identity == "client-other" or (required and identity is None)
        peers = []

        def protocol(*args, **kwargs):
            peer = module.Client(*args, **kwargs)
            peers.append(peer)
            return peer

        try:
            async with module.connect(
                "127.0.0.1", port, configuration=config, create_protocol=protocol
            ) as peer:
                reply = await asyncio.wait_for(peer.request("GET", "/tls-peer")["done"], 3)
                assert not rejected, "untrusted/absent required client dispatched"
                fields = dict(reply["sections"][0])
                assert fields[b"x-client-certificate"] == (b"present" if identity else b"absent")
                assert fields[b"x-client-verified"] == (b"yes" if identity else b"no")
                if identity:
                    assert (
                        fields[b"x-client-fingerprint"].decode()
                        == hashlib.sha256(
                            ssl.PEM_cert_to_DER_cert((fixtures / (identity + ".pem")).read_text())
                        ).hexdigest()
                    )
        except (ConnectionError, RuntimeError):
            assert rejected and peers and peers[0].terminal is not None
            expected = 0x130 if identity == "client-other" else 0x174
            assert peers[0].terminal.error_code == expected, peers[0].terminal
        rows.append(
            dict(
                case=("require" if required else "request") + "_" + str(identity),
                status="passed",
                rejected=rejected,
            )
        )
    return rows


async def independent_stores(first, second, first_port, second_port, fixtures, module):
    tickets=[]
    def config():
        value=module.configuration(fixtures);value.idle_timeout=5;return value
    async def hello(peer):
        result=await asyncio.wait_for(peer.request('GET','/hello')['done'],3)
        assert result['body']==b'hello native h3\n'
    async with module.connect('127.0.0.1',first_port,configuration=config(),create_protocol=module.Client,session_ticket_handler=tickets.append) as owner:
        await hello(owner);assert tickets
        original=hashlib.sha256(owner._quic.tls._peer_certificate.public_bytes(Encoding.DER)).hexdigest()
        shared_ticket=config();shared_ticket.session_ticket=tickets[0]
        async with module.connect('127.0.0.1',second_port,configuration=shared_ticket,create_protocol=module.Client) as other:
            await hello(other);assert not other.handshake.session_resumed
            assert hashlib.sha256(other._quic.tls._peer_certificate.public_bytes(Encoding.DER)).hexdigest()==original
        await command(first,'rotate','ROTATED')
        await hello(owner)
        async with module.connect('127.0.0.1',first_port,configuration=config(),create_protocol=module.Client) as fresh:
            await hello(fresh)
            assert hashlib.sha256(fresh._quic.tls._peer_certificate.public_bytes(Encoding.DER)).hexdigest()!=original
        async with module.connect('127.0.0.1',second_port,configuration=config(),create_protocol=module.Client) as unchanged:
            await hello(unchanged)
            assert hashlib.sha256(unchanged._quic.tls._peer_certificate.public_bytes(Encoding.DER)).hexdigest()==original
    return dict(case='public_quic_independent_stores',status='passed',foreign_ticket_resumed=False,
                other_store_preserved=True,existing_connection_preserved=True)
