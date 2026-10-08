/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <httpserver/detail/quic_invariant_header.hpp>
#include <httpserver/detail/quic_frame.hpp>
#include <httpserver/detail/quic_transport_parameters.hpp>
#include <httpserver/detail/quic_varint.hpp>
#include "fuzz/quic_parser_fuzz.hpp"
namespace {
namespace hd = httpserver::detail;
void require(bool value) {
    if (!value) std::abort();
}
}  // namespace
namespace {
void bounded(std::span<const std::byte> view, std::span<const std::byte> input) {
    if (view.empty()) return;
    const auto start = reinterpret_cast<std::uintptr_t>(input.data());
    const auto pointer = reinterpret_cast<std::uintptr_t>(view.data());
    require(pointer >= start && pointer - start <= input.size());
    require(view.size() <= input.size() - (pointer - start));
}
void frame_views(const hd::quic_frame& frame, std::span<const std::byte> input) {
    std::visit(
        [&](const auto& f) {
            if constexpr (requires { f.data; }) {
                bounded(f.data, input);
            }
            if constexpr (requires { f.token; }) {
                bounded(f.token, input);
            }
            if constexpr (requires { f.cid; }) {
                bounded(f.cid, input);
                bounded(f.reset_token, input);
            }
            if constexpr (requires { f.reason; }) {
                bounded(f.reason, input);
            }
            if constexpr (requires { f.encoded_ranges; }) {
                bounded(f.encoded_ranges, input);
                hd::quic_ack_cursor c;
                for (std::uint64_t i = 0; i <= f.range_count; ++i)
                    require(hd::next_quic_ack_range(f, c).code == hd::quic_codec_code::ok);
                require(c.offset == f.encoded_ranges.size());
            }
        },
        frame);
}
bool fuzz_frames(std::span<const std::byte> bytes, hd::quic_frame_context context) {
    hd::quic_frame_cursor cursor;
    std::array<std::byte, 8192> output{}, canonical{};
    while (cursor.offset < bytes.size()) {
        const auto saved = cursor;
        auto copy = cursor;
        auto a = hd::next_quic_frame(bytes, cursor, context, {4096, 256, 256, 64});
        auto b = hd::next_quic_frame(bytes, copy, context, {4096, 256, 256, 64});
        require(a.code == b.code && a.consumed == b.consumed);
        require(a.consumed <= bytes.size() - saved.offset);
        if (a.code != hd::quic_codec_code::ok) {
            require(cursor.offset == saved.offset && cursor.count == saved.count);
            return false;
        }
        require(a.consumed > 0 && cursor.count == saved.count + 1 && cursor.offset == saved.offset + a.consumed);
        frame_views(a.value, bytes);
        const auto written = hd::encode_quic_frame(a.value, output, context, {8192, 256, 256, 64});
        require(written.code == hd::quic_codec_code::ok);
        hd::quic_frame_cursor c;
        const auto round = hd::next_quic_frame(std::span(output).first(written.consumed), c, context, {8192, 256, 256, 64});
        require(round.code == hd::quic_codec_code::ok && round.value.index() == a.value.index() && c.offset == written.consumed);
        const auto twice = hd::encode_quic_frame(round.value, canonical, context, {8192, 256, 256, 64});
        require(twice.code == hd::quic_codec_code::ok && twice.consumed == written.consumed);
        require(std::equal(output.begin(), output.begin() + written.consumed, canonical.begin()));
    }
    return !bytes.empty();
}
void parameter_values(const hd::quic_transport_parameters& a, const hd::quic_transport_parameters& b) {
    require(a.present == b.present && a.disable_active_migration == b.disable_active_migration);
    require(a.max_idle_timeout == b.max_idle_timeout && a.max_udp_payload_size == b.max_udp_payload_size && a.initial_max_data == b.initial_max_data);
    require(a.initial_max_stream_data_bidi_local == b.initial_max_stream_data_bidi_local);
    require(a.initial_max_stream_data_bidi_remote == b.initial_max_stream_data_bidi_remote && a.initial_max_stream_data_uni == b.initial_max_stream_data_uni);
    require(a.initial_max_streams_bidi == b.initial_max_streams_bidi && a.initial_max_streams_uni == b.initial_max_streams_uni);
    require(a.ack_delay_exponent == b.ack_delay_exponent && a.max_ack_delay == b.max_ack_delay && a.active_connection_id_limit == b.active_connection_id_limit);
    const auto equal_view = [](auto x, auto y) { return std::equal(x.begin(), x.end(), y.begin(), y.end()); };
    const auto equal_optional = [&](auto x, auto y) { return x.has_value() == y.has_value() && (!x || equal_view(*x, *y)); };
    require(equal_optional(a.original_destination_cid, b.original_destination_cid));
    require(equal_optional(a.initial_source_cid, b.initial_source_cid) && equal_optional(a.retry_source_cid, b.retry_source_cid));
    require(equal_optional(a.stateless_reset_token, b.stateless_reset_token));
    require(a.preferred_address.has_value() == b.preferred_address.has_value());
    if (a.preferred_address) {
        const auto& x = *a.preferred_address;
        const auto& y = *b.preferred_address;
        require(x.ipv4 == y.ipv4 && x.ipv4_port == y.ipv4_port && x.ipv6 == y.ipv6 && x.ipv6_port == y.ipv6_port);
        require(equal_view(x.cid, y.cid) && equal_view(x.reset_token, y.reset_token));
    }
}
bool fuzz_parameters(std::span<const std::byte> bytes, hd::quic_endpoint_role role) {
    auto a = hd::decode_quic_transport_parameters(bytes, role, {4096, 256, 64, 64});
    auto b = hd::decode_quic_transport_parameters(bytes, role, {4096, 256, 64, 64});
    require(a.code == b.code && a.consumed == b.consumed && a.consumed <= bytes.size());
    if (a.code != hd::quic_codec_code::ok) return false;
    for (const auto& cid : {a.value.original_destination_cid, a.value.initial_source_cid, a.value.retry_source_cid, a.value.stateless_reset_token}) {
        if (cid) bounded(*cid, bytes);
    }
    if (a.value.preferred_address) {
        bounded(a.value.preferred_address->cid, bytes);
        bounded(a.value.preferred_address->reset_token, bytes);
    }
    std::array<std::byte, 8192> output{}, canonical{};
    auto w = hd::encode_quic_transport_parameters(a.value, role, output);
    require(w.code == hd::quic_codec_code::ok);
    auto round = hd::decode_quic_transport_parameters(std::span(output).first(w.consumed), role);
    require(round.code == hd::quic_codec_code::ok && round.consumed == w.consumed);
    // Unknown tuples and nonminimal widths may canonicalize; known values must survive.
    parameter_values(a.value, round.value);
    auto twice = hd::encode_quic_transport_parameters(round.value, role, canonical);
    require(twice.code == hd::quic_codec_code::ok && twice.consumed == w.consumed);
    require(std::equal(output.begin(), output.begin() + w.consumed, canonical.begin()));
    return true;
}
bool fuzz_packet(std::span<const std::byte> bytes) {
    auto a = hd::parse_quic_envelope(bytes, 2, {4096, 256, 64, 64});
    auto b = hd::parse_quic_envelope(bytes, 2, {4096, 256, 64, 64});
    require(a.code == b.code && a.consumed == b.consumed && a.consumed <= bytes.size());
    if (a.code != hd::quic_codec_code::ok) return false;
    const auto& e = a.value;
    for (auto view : {e.packet, e.destination, e.source, e.token, e.versions, e.integrity_tag, e.protected_remainder})
        bounded(view, bytes);
    require(a.consumed > 0);
    hd::quic_packet_write p;
    p.kind = e.kind;
    p.destination = e.destination;
    p.source = e.source;
    p.token = e.token;
    p.versions = e.versions;
    if (e.kind == hd::quic_packet_kind::retry) {
        p.tag = e.integrity_tag;
    } else if (e.kind != hd::quic_packet_kind::version_negotiation) {
        const auto first = std::to_integer<std::uint8_t>(e.packet[0]);
        const auto width = 1U + (first & 3);
        if (e.protected_remainder.size() < width + 16) return true;
        auto clear = hd::decode_quic_unprotected_header(e, {first, e.protected_remainder.first(width)}, {});
        require(clear.code == hd::quic_codec_code::ok);
        p.packet_number = clear.value.packet_number;
        p.packet_number_width = width;
        p.key_phase = clear.value.key_phase;
        p.spin = clear.value.spin;
        p.payload = e.protected_remainder.subspan(width, e.protected_remainder.size() - width - 16);
        p.tag = e.protected_remainder.last(16);
    }
    std::array<std::byte, 8192> output{};
    auto w = hd::encode_quic_packet(p, output);
    require(w.code == hd::quic_codec_code::ok);
    auto round = hd::parse_quic_envelope(std::span(output).first(w.consumed), 2);
    require(round.code == hd::quic_codec_code::ok && round.consumed == w.consumed && round.value.kind == e.kind);
    return true;
}
}  // namespace
std::uint32_t quic_codec_fuzz_input(std::span<const std::uint8_t> input) {
    input = input.first(std::min<std::size_t>(input.size(), 4096));
    const auto bytes = std::as_bytes(input);
    const auto scalar = hd::decode_quic_varint(bytes);
    require(scalar.consumed <= bytes.size());
    if (scalar.code == hd::quic_codec_code::ok) {
        std::array<std::byte, 8> out{};
        require(hd::encode_quic_varint(scalar.value, out).code == hd::quic_codec_code::ok);
        require(hd::decode_quic_varint(out).value == scalar.value);
        for (std::size_t width = 1; width <= 4; ++width) {
            hd::encode_quic_packet_number(scalar.value, width, out);
            hd::quic_cursor c(std::span(out).first(width));
            require(hd::reconstruct_quic_packet_number(c.fixed(width), width, scalar.value).code == hd::quic_codec_code::ok);
        }
    }
    std::uint32_t successes = fuzz_packet(bytes) ? 1 : 0;
    if (fuzz_frames(bytes, {})) successes |= 2;
    for (auto packet : {hd::quic_packet_kind::initial, hd::quic_packet_kind::handshake, hd::quic_packet_kind::zero_rtt}) {
        fuzz_frames(bytes, {packet, hd::quic_endpoint_role::client});
    }
    if (fuzz_parameters(bytes, hd::quic_endpoint_role::client)) successes |= 4;
    fuzz_parameters(bytes, hd::quic_endpoint_role::server);
    return successes;
}
std::size_t quic_parser_fuzz_input(std::span<const std::uint8_t> input) {
    input = input.first(std::min<std::size_t>(input.size(), 4096));
    const auto bytes = std::as_bytes(input);
    const std::array<std::optional<std::size_t>, 5> modes{std::nullopt, 0, 2, 20, 21};
    std::size_t successes = 0;
    for (auto length : modes) {
        const auto first = hd::extract_quic_invariant_header(bytes, length);
        const auto second = hd::extract_quic_invariant_header(bytes, length);
        require(first.has_value() == second.has_value());
        if (!first) continue;
        ++successes;
        require(first->destination.size <= 20 && first->source.size <= 20);
        require(first->destination == second->destination && first->source == second->source);
        require(first->version == second->version && first->long_header == second->long_header);
        require(!input.empty() && first->long_header == ((input[0] & 0x80) != 0));
        std::size_t destination_offset = 1;
        if (first->long_header) {
            require(input.size() >= 7);
            const auto version = (static_cast<std::uint32_t>(input[1]) << 24) | (static_cast<std::uint32_t>(input[2]) << 16) | (static_cast<std::uint32_t>(input[3]) << 8) | input[4];
            require(first->version == version && first->destination.size == input[5]);
            destination_offset = 6;
            const auto source_length_offset = destination_offset + first->destination.size;
            require(source_length_offset < input.size() && first->source.size == input[source_length_offset]);
            require(first->source.size <= input.size() - source_length_offset - 1);
            for (std::size_t i = 0; i < first->source.size; ++i)
                require(first->source.bytes[i] == bytes[source_length_offset + 1 + i]);
        } else {
            require(length && *length <= 20 && first->destination.size == *length);
            require(first->source.size == 0 && first->version == 0);
        }
        require(first->destination.size <= input.size() - destination_offset);
        for (std::size_t i = 0; i < first->destination.size; ++i)
            require(first->destination.bytes[i] == bytes[destination_offset + i]);
    }
    quic_codec_fuzz_input(input);
    return successes;
}
#ifdef QUIC_PARSER_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* bytes, std::size_t size) {
    quic_parser_fuzz_input({bytes, size});
    return 0;
}
#endif
