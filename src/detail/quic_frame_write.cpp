/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/quic_frame.hpp>
namespace httpserver {
namespace detail {
namespace {
struct frame_writer {
    quic_writer& w;
    quic_frame_context context;
    quic_codec_limits limits;
    void type(std::uint8_t value) noexcept {
        if (!quic_frame_allowed(value, context)) w.fail(quic_codec_code::malformed);
        w.fixed(value, 1);
    }
    void stream(std::uint64_t value, bool sending) noexcept {
        if (!quic_stream_sender_allowed(value, sending, context.sender)) w.fail(quic_codec_code::malformed);
        w.varint(value);
    }
    void data(std::uint64_t offset, std::span<const std::byte> bytes) noexcept {
        if (offset > k_quic_max_integer || bytes.size() > k_quic_max_integer - offset) w.fail(quic_codec_code::malformed);
        w.bytes(bytes);
    }
    void operator()(quic_padding_frame f) noexcept {
        if (!f.count) w.fail(quic_codec_code::malformed);
        if (!quic_frame_allowed(0, context)) w.fail(quic_codec_code::malformed);
        w.zeros(f.count);
    }
    void operator()(quic_ping_frame) noexcept { type(1); }
    void operator()(const quic_ack_frame& f) noexcept {
        type(f.ecn ? 3 : 2);
        if (f.range_count > limits.max_ack_ranges) {
            w.fail(quic_codec_code::limit_exceeded);
            return;
        }
        quic_ack_cursor c;
        for (std::uint64_t i = 0; i <= f.range_count; ++i) {
            const auto r = next_quic_ack_range(f, c);
            if (r.code != quic_codec_code::ok) {
                w.fail(r.code);
                return;
            }
        }
        if (c.offset != f.encoded_ranges.size()) {
            w.fail(quic_codec_code::malformed);
            return;
        }
        w.varint(f.largest);
        w.varint(f.delay);
        w.varint(f.range_count);
        w.varint(f.first_range);
        w.bytes(f.encoded_ranges);
        if (f.ecn)
            for (auto value : *f.ecn)
                w.varint(value);
    }
    void operator()(quic_reset_stream_frame f) noexcept {
        type(4);
        stream(f.stream, true);
        w.varint(f.error);
        w.varint(f.final_size);
    }
    void operator()(quic_stop_sending_frame f) noexcept {
        type(5);
        stream(f.stream, false);
        w.varint(f.error);
    }
    void operator()(const quic_crypto_frame& f) noexcept {
        type(6);
        w.varint(f.offset);
        w.varint(f.data.size());
        data(f.offset, f.data);
    }
    void operator()(const quic_new_token_frame& f) noexcept {
        type(7);
        if (f.token.empty()) w.fail(quic_codec_code::malformed);
        w.varint(f.token.size());
        w.bytes(f.token);
    }
    void operator()(const quic_stream_frame& f) noexcept {
        type(8 | (f.fin ? 1 : 0) | (f.has_length ? 2 : 0) | (f.has_offset ? 4 : 0));
        stream(f.stream, true);
        if (!f.has_offset && f.offset) w.fail(quic_codec_code::malformed);
        if (f.has_offset) w.varint(f.offset);
        if (f.has_length) w.varint(f.data.size());
        data(f.offset, f.data);
    }
    void operator()(quic_flow_frame f) noexcept {
        const auto value = static_cast<std::uint8_t>(f.kind);
        if (value < 16 || value > 23) {
            w.fail(quic_codec_code::malformed);
            return;
        }
        type(value);
        if (value == 17 || value == 21)
            stream(f.stream, value == 21);
        else if (f.stream)
            w.fail(quic_codec_code::malformed);
        if (quic_flow_is_stream_count(f.kind) && f.limit > (std::uint64_t{1} << 60)) w.fail(quic_codec_code::malformed);
        w.varint(f.limit);
    }
    void operator()(const quic_new_connection_id_frame& f) noexcept {
        type(24);
        if (f.cid.empty() || f.cid.size() > 20 || f.reset_token.size() != 16 || f.retire_prior_to > f.sequence) w.fail(quic_codec_code::malformed);
        w.varint(f.sequence);
        w.varint(f.retire_prior_to);
        w.fixed(f.cid.size(), 1);
        w.bytes(f.cid);
        w.bytes(f.reset_token);
    }
    void operator()(quic_retire_connection_id_frame f) noexcept {
        type(25);
        w.varint(f.sequence);
    }
    void operator()(const quic_path_frame& f) noexcept {
        type(f.response ? 27 : 26);
        if (f.data.size() != 8) w.fail(quic_codec_code::malformed);
        w.bytes(f.data);
    }
    void operator()(const quic_close_frame& f) noexcept {
        type(f.application ? 29 : 28);
        w.varint(f.error);
        if (f.application && f.frame_type) w.fail(quic_codec_code::malformed);
        if (!f.application) w.varint(f.frame_type);
        w.varint(f.reason.size());
        w.bytes(f.reason);
    }
    void operator()(quic_handshake_done_frame) noexcept { type(30); }
};
void emit_ack(quic_writer& w, std::span<const quic_ack_range> ranges, std::uint64_t delay, std::optional<std::array<std::uint64_t, 3>> ecn) noexcept {
    w.fixed(ecn ? 3 : 2, 1);
    w.varint(ranges[0].largest);
    w.varint(delay);
    w.varint(ranges.size() - 1);
    w.varint(ranges[0].largest - ranges[0].smallest);
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        const auto previous = ranges[i - 1].smallest;
        const auto& r = ranges[i];
        if (previous < 2 || r.largest > previous - 2) {
            w.fail(quic_codec_code::malformed);
            return;
        }
        w.varint(previous - r.largest - 2);
        w.varint(r.largest - r.smallest);
    }
    if (ecn)
        for (auto count : *ecn)
            w.varint(count);
}
}  // namespace
quic_encode_result encode_quic_frame(const quic_frame& frame, std::span<std::byte> output, quic_frame_context context, quic_codec_limits limits) noexcept {
    return quic_transactional_write(output, [&](quic_writer& w) {
        std::visit(frame_writer{w, context, limits}, frame);
        if (w.position() > limits.max_input_bytes) w.fail(quic_codec_code::limit_exceeded);
    });
}
quic_encode_result encode_quic_ack(std::span<const quic_ack_range> ranges, std::uint64_t delay, std::optional<std::array<std::uint64_t, 3>> ecn, std::span<std::byte> output,
                                   quic_codec_limits limits) noexcept {
    if (ranges.empty()) return {};
    if (ranges.size() - 1 > limits.max_ack_ranges) return {quic_codec_code::limit_exceeded};
    for (const auto& range : ranges)
        if (range.smallest > range.largest || range.largest > k_quic_max_integer) return {};
    return quic_transactional_write(output, [&](quic_writer& w) {
        emit_ack(w, ranges, delay, ecn);
        if (w.position() > limits.max_input_bytes) w.fail(quic_codec_code::limit_exceeded);
    });
}
}  // namespace detail
}  // namespace httpserver
