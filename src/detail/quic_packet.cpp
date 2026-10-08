/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/quic_packet.hpp>
#include <httpserver/detail/quic_varint.hpp>
namespace httpserver {
namespace detail {
namespace {
std::span<const std::byte> read_cid(quic_cursor& cursor) noexcept {
    const auto size = cursor.fixed(1);
    if (size > 20) cursor.fail(quic_codec_code::malformed);
    return cursor.take(size);
}
quic_codec_code parse_unprotected_body(quic_cursor& cursor, quic_packet_envelope& packet) noexcept {
    if (!packet.version) {
        packet.kind = quic_packet_kind::version_negotiation;
        if (!cursor.remaining() || cursor.remaining() % 4) return quic_codec_code::malformed;
        packet.versions = cursor.take(cursor.remaining());
    } else {
        if (cursor.remaining() < 16) return quic_codec_code::truncated;
        if (cursor.remaining() == 16) return quic_codec_code::malformed;
        packet.token = cursor.take(cursor.remaining() - 16);
        packet.integrity_tag = cursor.take(16);
    }
    return cursor.code();
}
quic_codec_code parse_long_body(quic_cursor& cursor, quic_packet_envelope& packet) noexcept {
    if (!packet.version || packet.kind == quic_packet_kind::retry) return parse_unprotected_body(cursor, packet);
    if (packet.kind == quic_packet_kind::initial) {
        const auto length = cursor.varint();
        packet.token = cursor.take(length);
    }
    const auto length = cursor.varint();
    if (cursor.code() != quic_codec_code::ok) return cursor.code();
    if (length < 17) return quic_codec_code::malformed;
    packet.packet_number_offset = cursor.position();
    packet.protected_remainder = cursor.take(length);
    return cursor.code();
}
quic_codec_code parse_long(quic_cursor& cursor, quic_packet_envelope& packet, std::uint64_t first) noexcept {
    packet.kind = static_cast<quic_packet_kind>((first >> 4) & 3);
    packet.version = cursor.fixed(4);
    packet.destination = read_cid(cursor);
    packet.source = read_cid(cursor);
    if (cursor.code() != quic_codec_code::ok) return cursor.code();
    if (packet.version && !(first & 0x40)) return quic_codec_code::malformed;
    if (packet.version > 1) return quic_codec_code::unsupported_version;
    return parse_long_body(cursor, packet);
}
quic_codec_code parse_short(quic_cursor& cursor, quic_packet_envelope& packet, std::uint64_t first, std::optional<std::size_t> cid_length) noexcept {
    if (!(first & 0x40) || !cid_length || *cid_length > 20) return quic_codec_code::malformed;
    packet.kind = quic_packet_kind::one_rtt;
    packet.destination = cursor.take(*cid_length);
    packet.packet_number_offset = cursor.position();
    if (cursor.code() != quic_codec_code::ok) return cursor.code();
    if (cursor.remaining() < 17) return quic_codec_code::truncated;
    packet.protected_remainder = cursor.take(cursor.remaining());
    return cursor.code();
}
std::uint8_t packet_first(quic_packet_kind kind) noexcept {
    switch (kind) {
        case quic_packet_kind::initial:
            return 0xc0;
        case quic_packet_kind::zero_rtt:
            return 0xd0;
        case quic_packet_kind::handshake:
            return 0xe0;
        case quic_packet_kind::retry:
            return 0xf0;
        case quic_packet_kind::version_negotiation:
            return 0x80;
        case quic_packet_kind::one_rtt:
            return 0x40;
    }
    return 0;
}
bool numbered(quic_packet_kind kind) noexcept {
    return kind == quic_packet_kind::initial || kind == quic_packet_kind::zero_rtt || kind == quic_packet_kind::handshake || kind == quic_packet_kind::one_rtt;
}
void emit_packet_prefix(quic_writer& writer, const quic_packet_write& packet) noexcept {
    auto first = packet_first(packet.kind);
    if (numbered(packet.kind)) first |= packet.packet_number_width - 1;
    if (packet.kind == quic_packet_kind::one_rtt) {
        if (packet.key_phase) first |= 4;
        if (packet.spin) first |= 0x20;
    }
    writer.fixed(first, 1);
    if (packet.kind != quic_packet_kind::one_rtt) {
        writer.fixed(packet.kind == quic_packet_kind::version_negotiation ? 0 : 1, 4);
        writer.fixed(packet.destination.size(), 1);
    }
    writer.bytes(packet.destination);
    if (packet.kind != quic_packet_kind::one_rtt) {
        writer.fixed(packet.source.size(), 1);
        writer.bytes(packet.source);
    }
}
bool valid_packet_metadata(const quic_packet_write& p) noexcept {
    if (p.kind == quic_packet_kind::one_rtt) return p.source.empty();
    return !(p.key_phase || p.spin);
}
bool valid_packet_views(const quic_packet_write& p) noexcept {
    if (p.destination.size() > 20 || p.source.size() > 20) return false;
    if (!valid_packet_metadata(p)) return false;
    const bool token_allowed = p.kind == quic_packet_kind::initial || p.kind == quic_packet_kind::retry;
    if (!token_allowed && !p.token.empty()) return false;
    return packet_first(p.kind) != 0;
}
bool valid_version_list(const quic_packet_write& p) noexcept {
    return !p.versions.empty() && p.versions.size() % 4 == 0 && p.tag.empty() && p.payload.empty();
}
bool valid_packet_body(const quic_packet_write& p) noexcept {
    if (p.kind == quic_packet_kind::version_negotiation) {
        return valid_version_list(p);
    }
    if (p.tag.size() != 16 || !p.versions.empty()) return false;
    if (p.kind == quic_packet_kind::retry) return !p.token.empty() && p.payload.empty();
    return p.packet_number_width >= 1 && p.packet_number_width <= 4 && p.packet_number <= k_quic_max_integer;
}
void emit_packet_body(quic_writer& writer, const quic_packet_write& p) noexcept {
    if (p.kind == quic_packet_kind::version_negotiation) {
        writer.bytes(p.versions);
        return;
    }
    if (p.kind == quic_packet_kind::retry) {
        writer.bytes(p.token);
        writer.bytes(p.tag);
        return;
    }
    if (p.kind == quic_packet_kind::initial) {
        writer.varint(p.token.size(), p.token_length_width);
        writer.bytes(p.token);
    }
    if (p.payload.size() > k_quic_max_integer - 16 - p.packet_number_width) {
        writer.fail(quic_codec_code::malformed);
        return;
    }
    if (p.kind != quic_packet_kind::one_rtt) writer.varint(p.packet_number_width + p.payload.size() + 16, p.length_width);
    writer.fixed(p.packet_number, p.packet_number_width);
    writer.bytes(p.payload);
    writer.bytes(p.tag);
}
}  // namespace
quic_decode_result<quic_packet_envelope> parse_quic_envelope(std::span<const std::byte> bytes, std::optional<std::size_t> short_length, quic_codec_limits limits) noexcept {
    if (bytes.size() > limits.max_input_bytes) return {quic_codec_code::limit_exceeded};
    quic_cursor cursor(bytes);
    quic_packet_envelope packet;
    const auto first = cursor.fixed(1);
    if (cursor.code() != quic_codec_code::ok) return {cursor.code()};
    const auto code = (first & 0x80) ? parse_long(cursor, packet, first) : parse_short(cursor, packet, first, short_length);
    if (code != quic_codec_code::ok) return {code};
    packet.packet = bytes.first(cursor.position());
    return {quic_codec_code::ok, cursor.position(), packet};
}
quic_decode_result<quic_clear_header> decode_quic_unprotected_header(const quic_packet_envelope& envelope, quic_unmasked_fields fields, std::optional<std::uint64_t> largest) noexcept {
    if (!numbered(envelope.kind)) return {};
    const auto width = std::size_t{1} + (fields.first & 3);
    // Only the protected bits may change when removing header protection.
    const auto mask = envelope.kind == quic_packet_kind::one_rtt ? 0xe0 : 0xf0;
    if (envelope.packet.empty() || (fields.first & mask) != (std::to_integer<unsigned>(envelope.packet[0]) & mask)) return {};
    if (fields.packet_number.size() != width || envelope.protected_remainder.size() < width + 16) return {};
    quic_cursor cursor(fields.packet_number);
    const auto pn = reconstruct_quic_packet_number(cursor.fixed(width), width, largest);
    if (pn.code != quic_codec_code::ok) return {pn.code};
    quic_clear_header header;
    header.kind = envelope.kind;
    header.packet_number = pn.value;
    header.packet_number_width = width;
    header.reserved = fields.first & (envelope.kind == quic_packet_kind::one_rtt ? 0x18 : 0x0c);
    if (envelope.kind == quic_packet_kind::one_rtt) {
        header.key_phase = fields.first & 4;
        header.spin = fields.first & 0x20;
    }
    return {quic_codec_code::ok, envelope.packet_number_offset + width, header};
}
quic_codec_code validate_quic_authenticated_header(const quic_clear_header& header) noexcept {
    return header.reserved ? quic_codec_code::malformed : quic_codec_code::ok;
}
quic_encode_result encode_quic_packet(const quic_packet_write& packet, std::span<std::byte> output) noexcept {
    if (!valid_packet_views(packet) || !valid_packet_body(packet)) return {};
    return quic_transactional_write(output, [&](quic_writer& writer) {
        emit_packet_prefix(writer, packet);
        emit_packet_body(writer, packet);
    });
}
}  // namespace detail
}  // namespace httpserver
