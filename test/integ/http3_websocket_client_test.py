"""Independent RFC 6455 wire vectors; no QUIC dependency."""
import importlib
import unittest


class WireTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('http3_websocket_wire'), 'independent wire helper missing')
        self.w = importlib.import_module('http3_websocket_wire')

    def test_masked_vectors_and_fragment_flags(self):
        self.assertEqual(self.w.frame(1, b'Hello', key=bytes.fromhex('37fa213d')).hex(), '818537fa213d7f9f4d5158')
        self.assertEqual(self.w.frame(2, b'', key=b'abcd'), b'\x82\x80abcd')
        self.assertEqual(self.w.frame(1, b'x', fin=False, key=b'\0'*4), b'\x01\x81'+b'\0'*4+b'x')
        self.assertEqual(self.w.frame(0, b'y', masked=False), b'\x80\x01y')
        for n, head in [(125, b'\x82\xfd'), (126, b'\x82\xfe\0\x7e')]:
            self.assertTrue(self.w.frame(2, b'x'*n, key=b'abcd').startswith(head))

    def test_incremental_and_coalesced_server_frames(self):
        decoder = self.w.Decoder()
        wire = b'\x82\x7e\0\x7e' + bytes(range(126)) + b'\x8a\x01p'
        frames = []
        for byte in wire:
            frames.extend(decoder.feed(bytes([byte])))
        self.assertEqual([(f.opcode, f.fin, f.payload, f.masked) for f in frames], [(2, True, bytes(range(126)), False), (10, True, b'p', False)])
        decoder.finish()
        self.assertEqual(len(self.w.Decoder().feed(b'\x81\0\x82\0')), 2)

    def test_invalid_server_wire_rejected_before_payload_allocation(self):
        for wire in [b'\x81\x80', b'\xc1\0', b'\x09\0', b'\x89\x7e\0\x7e', b'\x83\0', b'\x82\x7e\0\x01', b'\x82\x7f'+(1<<63).to_bytes(8,'big'), b'\x82\x7e\x40\0']:
            with self.subTest(wire=wire), self.assertRaises(ValueError):
                self.w.Decoder(max_payload=8192).feed(wire)

    def test_terminal_truncation_and_buffer_bound(self):
        decoder = self.w.Decoder()
        decoder.feed(b'\x82\x03a')
        with self.assertRaises(ValueError): decoder.finish()
        with self.assertRaises(ValueError): self.w.Decoder().feed(b'x'*1048577)

    def test_close_code_reason_and_strict_payload(self):
        self.assertEqual(self.w.close_payload(b'\x03\xe8bye'), (1000, 'bye'))
        self.assertEqual(self.w.close_payload(b''), (None, ''))
        for value in [b'x', b'\x03\xed', b'\x03\xe8\xff']:
            with self.assertRaises(ValueError): self.w.close_payload(value)
        with self.assertRaises(AssertionError): self.w.expect(self.w.Frame(2, True, b'x'), 2, b'y')



@unittest.skipUnless(importlib.util.find_spec('aioquic'), 'pinned aioquic environment required')
class ProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.assertIsNotNone(importlib.util.find_spec('http3_websocket_aioquic'), 'external RFC 9220 client missing')
        self.m = importlib.import_module('http3_websocket_aioquic')
        from aioquic.quic.configuration import QuicConfiguration
        from aioquic.quic.connection import QuicConnection
        self.client = self.m.WebSocketClient(QuicConnection(configuration=QuicConfiguration(is_client=True, alpn_protocols=['h3'])))
        self.client.transmit = lambda: None

    async def test_initialization_opens_exactly_one_set_of_h3_critical_streams(self):
        self.assertEqual(self.client._quic._local_next_stream_id_uni, 14)

    async def test_settings_observer_records_native_qlog_after_validation(self):
        from aioquic.quic.logger import QuicLogger
        from aioquic.quic.configuration import QuicConfiguration
        from aioquic.quic.connection import QuicConnection
        from aioquic.h3.connection import FrameType
        logger = QuicLogger()
        client = self.m.WebSocketClient(QuicConnection(configuration=QuicConfiguration(is_client=True, alpn_protocols=['h3'], quic_logger=logger)))
        client.h3._handle_control_frame(FrameType.SETTINGS, b'\x08\x01')
        rows=logger.to_dict()['traces'][0]['events']
        observed=[e for e in rows if e['name']=='http:settings_received']
        self.assertEqual(len(observed),1)
        self.assertEqual(observed[0]['data']['settings'],{'8':1})

    async def test_connect_requires_actual_enabling_settings(self):
        with self.assertRaises(RuntimeError): self.client.open_ws('/ws/echo')
        from aioquic.h3.connection import FrameType
        self.client.h3._handle_control_frame(FrameType.SETTINGS, b'\x08\x01')
        state = self.client.open_ws('/ws/echo')
        self.assertEqual(self.client.h3.observed_settings, {8: 1})
        self.assertEqual(state.stream % 4, 0)
        self.assertIn(state.stream, self.client.websockets)

    async def test_segmentation_and_terminal_future_retirement(self):
        from aioquic.h3.connection import FrameType
        from aioquic.h3.events import DataReceived
        from aioquic.quic.events import StreamReset
        self.client.h3._handle_control_frame(FrameType.SETTINGS, b'\x08\x01')
        state = self.client.open_ws('/ws/echo')
        self.client.observe(DataReceived(data=b'\x82\x03a', stream_id=state.stream, stream_ended=False))
        self.assertTrue(state.frames.empty())
        self.client.observe(DataReceived(data=b'bc', stream_id=state.stream, stream_ended=False))
        self.assertEqual((await state.next_frame()).payload, b'abc')
        self.client.quic_event_received(StreamReset(error_code=268, stream_id=state.stream))
        with self.assertRaises(self.m.StreamFailure): await state.terminal
        self.assertNotIn(state.stream, self.client.websockets)
        with self.assertRaises(self.m.StreamFailure): await state.next_frame()

    async def test_terminal_truncation_fails_observer(self):
        from aioquic.h3.connection import FrameType
        from aioquic.h3.events import DataReceived
        self.client.h3._handle_control_frame(FrameType.SETTINGS, b'\x08\x01')
        state = self.client.open_ws('/ws/echo')
        with self.assertRaises(ValueError):
            self.client.observe(DataReceived(data=b'\x82\x03a', stream_id=state.stream, stream_ended=True))

    async def test_http_pending_bound_and_terminal_retirement(self):
        from aioquic.quic.events import ConnectionTerminated
        states=[self.client.request('GET','/health') for _ in range(16)]
        with self.assertRaises(RuntimeError): self.client.request('GET','/health')
        self.client.quic_event_received(ConnectionTerminated(error_code=1, frame_type=None, reason_phrase='test'))
        self.assertFalse(self.client.pending)
        for state in states:
            with self.assertRaises(self.m.StreamFailure): await state['done']

    async def test_handshake_refuses_extensions_and_http1_fields(self):
        for extra in [[(b'upgrade', b'websocket')], [(b'sec-websocket-extensions', b'permessage-deflate')], [(b'sec-websocket-protocol', b'other')]]:
            with self.subTest(extra=extra), self.assertRaises(AssertionError):
                self.m.verify_upgrade([(b':status', b'200'), (b'sec-websocket-protocol', b'harness.v1'), *extra])


if __name__ == '__main__': unittest.main()
