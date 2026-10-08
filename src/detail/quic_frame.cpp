/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/quic_frame.hpp>
#include <httpserver/detail/quic_varint.hpp>
namespace httpserver {
namespace detail {
namespace {
std::uint8_t packet_mask(quic_packet_kind kind) noexcept {
    switch (kind) {
        case quic_packet_kind::initial:
            return 1;
        case quic_packet_kind::handshake:
            return 2;
        case quic_packet_kind::zero_rtt:
            return 4;
        case quic_packet_kind::one_rtt:
            return 8;
        default:
            return 0;
    }
}
}  // namespace
bool quic_frame_allowed(std::uint8_t type, quic_frame_context context) noexcept {
    // RFC 9000 Table 3: Initial=1, Handshake=2, 0-RTT=4, 1-RTT=8.
    constexpr std::array<std::uint8_t, 31> masks{15, 15, 11, 11, 12, 12, 11, 8, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 8, 15, 12, 8};
    if (type >= masks.size()) return false;
    if ((type == 7 || type == 30) && context.sender == quic_endpoint_role::client) return false;
    return (masks[type] & packet_mask(context.packet)) != 0;
}
bool quic_flow_is_stream_count(quic_flow_kind kind) noexcept {
    return kind == quic_flow_kind::max_streams_bidi || kind == quic_flow_kind::max_streams_uni || kind == quic_flow_kind::streams_blocked_bidi || kind == quic_flow_kind::streams_blocked_uni;
}
bool quic_stream_sender_allowed(std::uint64_t stream, bool sending, quic_endpoint_role sender) noexcept {
    if (!(stream & 2)) return true;
    const auto initiator = (stream & 1) ? quic_endpoint_role::server : quic_endpoint_role::client;
    return (initiator == sender) == sending;
}
namespace {
void check_data_end(quic_cursor& c, std::uint64_t offset, std::size_t length) noexcept {
    if (length > k_quic_max_integer - offset) c.fail(quic_codec_code::malformed);
}
void check_stream(quic_cursor& c, std::uint64_t stream, bool sending, quic_frame_context context) noexcept {
    if (!quic_stream_sender_allowed(stream, sending, context.sender)) c.fail(quic_codec_code::malformed);
}
std::uint64_t read_ack_range(quic_cursor& c, std::uint64_t previous) noexcept {
    const auto gap = c.varint(), length = c.varint();
    if (previous < 2 || gap > previous - 2) {
        c.fail(quic_codec_code::malformed);
        return 0;
    }
    const auto largest = previous - gap - 2;
    if (length > largest) {
        c.fail(quic_codec_code::malformed);
        return 0;
    }
    return largest - length;
}
quic_ack_frame read_ack(quic_cursor& c, std::span<const std::byte> bytes, bool ecn, quic_codec_limits limits) noexcept {
    quic_ack_frame ack;
    ack.largest = c.varint();
    ack.delay = c.varint();
    ack.range_count = c.varint();
    ack.first_range = c.varint();
    if (c.code() != quic_codec_code::ok) return ack;
    if (ack.first_range > ack.largest) {
        c.fail(quic_codec_code::malformed);
        return ack;
    }
    if (ack.range_count > limits.max_ack_ranges) {
        c.fail(quic_codec_code::limit_exceeded);
        return ack;
    }
    if (ack.range_count > c.remaining() / 2) {
        c.fail(quic_codec_code::truncated);
        return ack;
    }
    const auto start = c.position();
    auto smallest = ack.largest - ack.first_range;
    for (std::uint64_t i = 0; i < ack.range_count && c.code() == quic_codec_code::ok; ++i) {
        smallest = read_ack_range(c, smallest);
    }
    if (c.code() == quic_codec_code::ok) ack.encoded_ranges = bytes.subspan(start, c.position() - start);
    if (ecn) ack.ecn = std::array<std::uint64_t, 3>{c.varint(), c.varint(), c.varint()};
    return ack;
}
quic_frame read_small(std::uint8_t type, quic_cursor& c, quic_frame_context context) noexcept {
    switch (type) {
        case 4: {
            quic_reset_stream_frame f{c.varint(), c.varint(), c.varint()};
            check_stream(c, f.stream, true, context);
            return f;
        }
        case 5: {
            quic_stop_sending_frame f{c.varint(), c.varint()};
            check_stream(c, f.stream, false, context);
            return f;
        }
        case 6: {
            quic_crypto_frame f;
            f.offset = c.varint();
            const auto length = c.varint();
            f.data = c.take(length);
            check_data_end(c, f.offset, f.data.size());
            return f;
        }
        default: {
            const auto length = c.varint();
            if (!length) c.fail(quic_codec_code::malformed);
            return quic_new_token_frame{c.take(length)};
        }
    }
}
quic_stream_frame read_stream(std::uint8_t type, quic_cursor& c, quic_frame_context context) noexcept {
    quic_stream_frame f;
    f.fin = type & 1;
    f.has_length = type & 2;
    f.has_offset = type & 4;
    f.stream = c.varint();
    if (f.has_offset) f.offset = c.varint();
    const auto length = f.has_length ? c.varint() : c.remaining();
    f.data = c.take(length);
    check_data_end(c, f.offset, f.data.size());
    check_stream(c, f.stream, true, context);
    return f;
}
quic_flow_frame read_flow(std::uint8_t type, quic_cursor& c, quic_frame_context context) noexcept {
    quic_flow_frame f;
    f.kind = static_cast<quic_flow_kind>(type);
    if (type == 17 || type == 21) {
        f.stream = c.varint();
        check_stream(c, f.stream, type == 21, context);
    }
    f.limit = c.varint();
    if (quic_flow_is_stream_count(f.kind) && f.limit > (std::uint64_t{1} << 60)) c.fail(quic_codec_code::malformed);
    return f;
}
quic_frame read_connection(std::uint8_t type, quic_cursor& c) noexcept {
    if (type == 24) {
        quic_new_connection_id_frame f;
        f.sequence = c.varint();
        f.retire_prior_to = c.varint();
        const auto size = c.fixed(1);
        if (!size || size > 20 || f.retire_prior_to > f.sequence) c.fail(quic_codec_code::malformed);
        f.cid = c.take(size);
        f.reset_token = c.take(16);
        return f;
    }
    if (type == 25) return quic_retire_connection_id_frame{c.varint()};
    return quic_path_frame{type == 27, c.take(8)};
}
quic_frame read_close(std::uint8_t type, quic_cursor& c) noexcept {
    if (type == 30) return quic_handshake_done_frame{};
    quic_close_frame f;
    f.application = type == 29;
    f.error = c.varint();
    if (!f.application) f.frame_type = c.varint();
    const auto length = c.varint();
    f.reason = c.take(length);
    return f;
}
quic_frame read_frame(std::uint8_t type, quic_cursor& c, std::span<const std::byte> bytes, quic_frame_context context, quic_codec_limits limits) noexcept {
    if (type == 0) {
        std::size_t count = 1;
        while (c.remaining() && bytes[c.position()] == std::byte{0}) {
            c.take(1);
            ++count;
        }
        return quic_padding_frame{count};
    }
    if (type == 1) return quic_ping_frame{};
    if (type <= 3) return read_ack(c, bytes, type == 3, limits);
    if (type <= 7) return read_small(type, c, context);
    if (type <= 15) return read_stream(type, c, context);
    if (type <= 23) return read_flow(type, c, context);
    if (type <= 27) return read_connection(type, c);
    return read_close(type, c);
}
}  // namespace
quic_decode_result<quic_frame> next_quic_frame(std::span<const std::byte> bytes, quic_frame_cursor& cursor, quic_frame_context context, quic_codec_limits limits) noexcept {
    if (bytes.size() > limits.max_input_bytes || cursor.count >= limits.max_frames) return {quic_codec_code::limit_exceeded};
    if (cursor.offset > bytes.size()) return {};
    quic_cursor c(bytes.subspan(cursor.offset));
    const auto type = c.varint();
    if (c.code() != quic_codec_code::ok) return {c.code()};
    if (c.position() != quic_varint_width(type) || type > 30) return {};
    if (!quic_frame_allowed(type, context)) return {};
    auto frame = read_frame(type, c, bytes.subspan(cursor.offset), context, limits);
    if (c.code() != quic_codec_code::ok) return {c.code()};
    cursor.offset += c.position();
    ++cursor.count;
    return {quic_codec_code::ok, c.position(), frame};
}
quic_decode_result<quic_ack_range> next_quic_ack_range(const quic_ack_frame& ack, quic_ack_cursor& cursor) noexcept {
    if (cursor.index > ack.range_count) return {quic_codec_code::truncated};
    auto next = cursor;
    quic_ack_range range;
    if (!next.index) {
        if (ack.first_range > ack.largest) return {};
        range = {ack.largest - ack.first_range, ack.largest};
    } else {
        if (next.offset > ack.encoded_ranges.size()) return {};
        quic_cursor c(ack.encoded_ranges.subspan(next.offset));
        const auto gap = c.varint(), length = c.varint();
        if (c.code() != quic_codec_code::ok) return {c.code()};
        if (next.smallest < 2 || gap > next.smallest - 2) return {};
        range.largest = next.smallest - gap - 2;
        if (length > range.largest) return {};
        range.smallest = range.largest - length;
        next.offset += c.position();
    }
    const auto consumed = next.offset - cursor.offset;
    next.smallest = range.smallest;
    ++next.index;
    cursor = next;
    return {quic_codec_code::ok, consumed, range};
}
}  // namespace detail
}  // namespace httpserver
