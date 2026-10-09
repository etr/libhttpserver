"""The pinned H3 client must validate HEAD without discarding length metadata."""
import unittest
from aioquic.h3.connection import H3Stream, MessageError
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.connection import QuicConnection
import behavior_parity_h3


class HeadValidation(unittest.TestCase):
    def setUp(self):
        adapter = getattr(behavior_parity_h3, 'HeadAwareH3', None)
        self.assertIsNotNone(adapter, 'pinned client has no HEAD request context')
        self.connection = adapter(QuicConnection(configuration=QuicConfiguration(is_client=True)))

    def test_head_accepts_representation_length_without_data(self):
        stream = H3Stream(0)
        stream.expected_content_length = 7
        self.connection.head_streams.add(0)
        self.connection._check_content_length(stream)
        self.assertEqual(7, stream.expected_content_length)

    def test_head_data_is_rejected_even_when_it_matches_representation_length(self):
        stream = H3Stream(0)
        stream.expected_content_length = 7
        stream.content_length = 7
        self.connection.head_streams.add(0)
        with self.assertRaises(MessageError):
            self.connection._check_content_length(stream)

    def test_get_still_requires_exact_declared_body_length(self):
        stream = H3Stream(0)
        stream.expected_content_length = 7
        with self.assertRaises(MessageError):
            self.connection._check_content_length(stream)
        stream.content_length = 7
        self.connection._check_content_length(stream)


if __name__ == '__main__':
    unittest.main()
