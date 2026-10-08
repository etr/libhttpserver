/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/quic_transport_parameters.hpp>
#include <httpserver/detail/quic_varint.hpp>
namespace httpserver {
namespace detail {
namespace {
using parameters = quic_transport_parameters;
constexpr std::array<std::uint64_t parameters::*, 17> integers{
    nullptr, &parameters::max_idle_timeout, nullptr, &parameters::max_udp_payload_size,
    &parameters::initial_max_data, &parameters::initial_max_stream_data_bidi_local,
    &parameters::initial_max_stream_data_bidi_remote, &parameters::initial_max_stream_data_uni,
    &parameters::initial_max_streams_bidi, &parameters::initial_max_streams_uni,
    &parameters::ack_delay_exponent, &parameters::max_ack_delay, nullptr, nullptr,
    &parameters::active_connection_id_limit, nullptr, nullptr
};
bool valid_integer(std::size_t id, std::uint64_t value) noexcept {
    if (value > k_quic_max_integer) return false;
    switch (id) {
        case 3:
            return value >= 1200;
        case 8:
        case 9:
            return value <= (std::uint64_t{1} << 60);
        case 10:
            return value <= 20;
        case 11:
            return value < 16384;
        case 14:
            return value >= 2;
        default:
            return true;
    }
}
bool server_only(std::uint64_t id) noexcept {
    return id == 0 || id == 2 || id == 13 || id == 16;
}
quic_preferred_address read_preferred(quic_cursor& c) noexcept {
    quic_preferred_address p;
    const auto ipv4 = c.take(4);
    std::copy(ipv4.begin(), ipv4.end(), p.ipv4.begin());
    p.ipv4_port = c.fixed(2);
    const auto ipv6 = c.take(16);
    std::copy(ipv6.begin(), ipv6.end(), p.ipv6.begin());
    p.ipv6_port = c.fixed(2);
    const auto size = c.fixed(1);
    if (!size || size > 20) c.fail(quic_codec_code::malformed);
    p.cid = c.take(size);
    p.reset_token = c.take(16);
    return p;
}
quic_codec_code read_cid_parameter(parameters& p, std::size_t id, std::span<const std::byte> value) noexcept {
    if (value.size() > 20) return quic_codec_code::malformed;
    switch (id) {
        case 0:
            p.original_destination_cid = value;
            break;
        case 15:
            p.initial_source_cid = value;
            break;
        case 16:
            p.retry_source_cid = value;
            break;
    }
    return quic_codec_code::ok;
}
quic_codec_code read_preferred_parameter(parameters& p, std::span<const std::byte> value) noexcept {
    quic_cursor c(value);
    auto preferred = read_preferred(c);
    if (c.code() != quic_codec_code::ok || c.remaining()) return quic_codec_code::malformed;
    p.preferred_address = preferred;
    return quic_codec_code::ok;
}
quic_codec_code read_special(parameters& p, std::size_t id, std::span<const std::byte> value) noexcept {
    switch (id) {
        case 0:
        case 15:
        case 16:
            return read_cid_parameter(p, id, value);
        case 2:
            if (value.size() != 16) return quic_codec_code::malformed;
            p.stateless_reset_token = value;
            break;
        case 12:
            if (!value.empty()) return quic_codec_code::malformed;
            p.disable_active_migration = true;
            break;
        case 13:
            return read_preferred_parameter(p, value);
    }
    return quic_codec_code::ok;
}
quic_codec_code read_parameter(parameters& p, std::uint64_t id, std::span<const std::byte> value, quic_endpoint_role sender) noexcept {
    if (sender == quic_endpoint_role::client && server_only(id)) return quic_codec_code::malformed;
    if (id >= integers.size()) return quic_codec_code::ok;
    p.present |= std::uint32_t{1} << id;
    if (!integers[id]) return read_special(p, id, value);
    const auto decoded = decode_quic_varint(value);
    if (decoded.code != quic_codec_code::ok || decoded.consumed != value.size() || !valid_integer(id, decoded.value)) return quic_codec_code::malformed;
    p.*integers[id] = decoded.value;
    return quic_codec_code::ok;
}
bool cid_shape(const std::optional<std::span<const std::byte>>& cid) noexcept {
    return !cid || cid->size() <= 20;
}
bool valid_preferred(const std::optional<quic_preferred_address>& p) noexcept {
    return !p || (!p->cid.empty() && p->cid.size() <= 20 && p->reset_token.size() == 16);
}
bool valid_shapes(const parameters& p) noexcept {
    if (!cid_shape(p.original_destination_cid) || !cid_shape(p.initial_source_cid) || !cid_shape(p.retry_source_cid)) return false;
    if (p.stateless_reset_token && p.stateless_reset_token->size() != 16) return false;
    if (p.preferred_address && p.initial_source_cid && p.initial_source_cid->empty()) return false;
    return valid_preferred(p.preferred_address);
}
bool valid_sender(const parameters& p, quic_endpoint_role sender) noexcept {
    if (sender == quic_endpoint_role::server) return true;
    return !p.original_destination_cid && !p.stateless_reset_token && !p.preferred_address && !p.retry_source_cid;
}
bool same_cid(std::optional<std::span<const std::byte>> actual, std::span<const std::byte> expected) noexcept {
    return actual && actual->size() == expected.size() && std::equal(actual->begin(), actual->end(), expected.begin());
}
void tuple(quic_writer& w, std::size_t id, std::span<const std::byte> value) noexcept {
    w.varint(id);
    w.varint(value.size());
    w.bytes(value);
}
void cid_tuple(quic_writer& w, std::size_t id, const std::optional<std::span<const std::byte>>& value) noexcept {
    if (value) tuple(w, id, *value);
}
void preferred_tuple(quic_writer& w, const quic_preferred_address& p) noexcept {
    w.varint(13);
    w.varint(41 + p.cid.size());
    w.bytes(p.ipv4);
    w.fixed(p.ipv4_port, 2);
    w.bytes(p.ipv6);
    w.fixed(p.ipv6_port, 2);
    w.fixed(p.cid.size(), 1);
    w.bytes(p.cid);
    w.bytes(p.reset_token);
}
void emit_special(quic_writer& w, const parameters& p, std::size_t id) noexcept {
    switch (id) {
        case 0:
            cid_tuple(w, id, p.original_destination_cid);
            break;
        case 2:
            cid_tuple(w, id, p.stateless_reset_token);
            break;
        case 12:
            if (p.disable_active_migration) tuple(w, id, {});
            break;
        case 13:
            if (p.preferred_address) preferred_tuple(w, *p.preferred_address);
            break;
        case 15:
            cid_tuple(w, id, p.initial_source_cid);
            break;
        case 16:
            cid_tuple(w, id, p.retry_source_cid);
            break;
    }
}
void emit_parameters(quic_writer& w, const parameters& p, quic_codec_limits limits) noexcept {
    const parameters defaults;
    std::size_t count = 0;
    for (std::size_t id = 0; id < integers.size(); ++id) {
        const auto start = w.position();
        if (integers[id]) {
            const auto value = p.*integers[id];
            if (!valid_integer(id, value)) {
                w.fail(quic_codec_code::malformed);
                return;
            }
            if (value != defaults.*integers[id] || (p.present & (std::uint32_t{1} << id))) {
                w.varint(id);
                w.varint(quic_varint_width(value));
                w.varint(value);
            }
        } else {
            emit_special(w, p, id);
        }
        if (w.position() != start) ++count;
    }
    if (count > std::min<std::size_t>(limits.max_parameters, 256) || w.position() > limits.max_input_bytes) w.fail(quic_codec_code::limit_exceeded);
}
}  // namespace
quic_decode_result<parameters> decode_quic_transport_parameters(std::span<const std::byte> input, quic_endpoint_role sender, quic_codec_limits limits) noexcept {
    if (input.size() > limits.max_input_bytes) return {quic_codec_code::limit_exceeded};
    parameters p;
    std::array<std::uint64_t, 256> seen{};
    std::size_t count = 0;
    quic_cursor c(input);
    while (c.remaining()) {
        const auto id = c.varint(), length = c.varint();
        const auto value = c.take(length);
        if (c.code() != quic_codec_code::ok) return {c.code()};
        if (std::find(seen.begin(), seen.begin() + count, id) != seen.begin() + count) return {};
        if (count >= std::min<std::size_t>(limits.max_parameters, seen.size())) return {quic_codec_code::limit_exceeded};
        seen[count++] = id;
        const auto code = read_parameter(p, id, value, sender);
        if (code != quic_codec_code::ok) return {code};
    }
    if (!valid_shapes(p)) return {};
    return {quic_codec_code::ok, input.size(), p};
}
quic_codec_code validate_quic_transport_parameters(const parameters& p, quic_endpoint_role sender, const quic_parameter_cid_context& context) noexcept {
    if (!valid_shapes(p) || !valid_sender(p, sender) || !same_cid(p.initial_source_cid, context.initial_source)) return quic_codec_code::malformed;
    if (sender == quic_endpoint_role::server) {
        if (!same_cid(p.original_destination_cid, context.original_destination)) return quic_codec_code::malformed;
        if (p.retry_source_cid.has_value() != context.retry_source.has_value()) return quic_codec_code::malformed;
        if (context.retry_source && !same_cid(p.retry_source_cid, *context.retry_source)) return quic_codec_code::malformed;
    }
    return quic_codec_code::ok;
}
quic_encode_result encode_quic_transport_parameters(const parameters& p, quic_endpoint_role sender, std::span<std::byte> output, quic_codec_limits limits) noexcept {
    if (!valid_shapes(p) || !valid_sender(p, sender)) return {};
    return quic_transactional_write(output, [&](quic_writer& w) { emit_parameters(w, p, limits); });
}
}  // namespace detail
}  // namespace httpserver
