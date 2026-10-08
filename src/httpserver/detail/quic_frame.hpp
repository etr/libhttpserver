/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_frame.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_FRAME_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_FRAME_HPP_
#include <array>
#include <optional>
#include <variant>
#include <httpserver/detail/quic_packet.hpp>
namespace httpserver {
namespace detail {
struct quic_padding_frame {
    std::size_t count = 1;
};
struct quic_ping_frame {};
struct quic_ack_range {
    std::uint64_t smallest = 0, largest = 0;
};
struct quic_ack_frame {
    std::uint64_t largest = 0, delay = 0, first_range = 0, range_count = 0;
    std::span<const std::byte> encoded_ranges;
    std::optional<std::array<std::uint64_t, 3>> ecn;
};
struct quic_reset_stream_frame {
    std::uint64_t stream = 0, error = 0, final_size = 0;
};
struct quic_stop_sending_frame {
    std::uint64_t stream = 0, error = 0;
};
struct quic_crypto_frame {
    std::uint64_t offset = 0;
    std::span<const std::byte> data;
};
struct quic_new_token_frame {
    std::span<const std::byte> token;
};
struct quic_stream_frame {
    std::uint64_t stream = 0, offset = 0;
    std::span<const std::byte> data;
    bool fin = false, has_offset = false, has_length = true;
};
enum class quic_flow_kind : std::uint8_t { max_data = 0x10, max_stream_data, max_streams_bidi, max_streams_uni, data_blocked, stream_data_blocked, streams_blocked_bidi, streams_blocked_uni };
struct quic_flow_frame {
    quic_flow_kind kind = quic_flow_kind::max_data;
    std::uint64_t limit = 0, stream = 0;
};
struct quic_new_connection_id_frame {
    std::uint64_t sequence = 0, retire_prior_to = 0;
    std::span<const std::byte> cid, reset_token;
};
struct quic_retire_connection_id_frame {
    std::uint64_t sequence = 0;
};
struct quic_path_frame {
    bool response = false;
    std::span<const std::byte> data;
};
struct quic_close_frame {
    bool application = false;
    std::uint64_t error = 0, frame_type = 0;
    std::span<const std::byte> reason;
};
struct quic_handshake_done_frame {};
using quic_frame = std::variant<quic_padding_frame, quic_ping_frame, quic_ack_frame, quic_reset_stream_frame,
quic_stop_sending_frame, quic_crypto_frame, quic_new_token_frame, quic_stream_frame,
quic_flow_frame, quic_new_connection_id_frame, quic_retire_connection_id_frame,
quic_path_frame, quic_close_frame, quic_handshake_done_frame>;
struct quic_frame_context {
    quic_packet_kind packet = quic_packet_kind::one_rtt;
    quic_endpoint_role sender = quic_endpoint_role::server;
};
struct quic_frame_cursor {
    std::size_t offset = 0, count = 0;
};
struct quic_ack_cursor {
    std::size_t index = 0, offset = 0;
    std::uint64_t smallest = 0;
};
bool quic_flow_is_stream_count(quic_flow_kind kind) noexcept;
bool quic_frame_allowed(std::uint8_t type, quic_frame_context context) noexcept;
bool quic_stream_sender_allowed(std::uint64_t stream, bool sending, quic_endpoint_role sender) noexcept;
// All payload/range views borrow input. Cursor changes only on success.
quic_decode_result<quic_frame> next_quic_frame(std::span<const std::byte> payload, quic_frame_cursor& cursor, quic_frame_context context = {}, quic_codec_limits limits = {}) noexcept;
// The first range is already in the decoded prefix and consumes zero range
// bytes; index still advances. Additional calls consume one encoded pair.
quic_decode_result<quic_ack_range> next_quic_ack_range(const quic_ack_frame& ack, quic_ack_cursor& cursor) noexcept;
quic_encode_result encode_quic_frame(const quic_frame& frame, std::span<std::byte> output, quic_frame_context context = {}, quic_codec_limits limits = {}) noexcept;
// Ranges are inclusive, descending and disjoint with at least one missing PN.
quic_encode_result encode_quic_ack(std::span<const quic_ack_range> ranges, std::uint64_t delay, std::optional<std::array<std::uint64_t, 3>> ecn, std::span<std::byte> output,
    quic_codec_limits limits = {}) noexcept;
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_FRAME_HPP_
