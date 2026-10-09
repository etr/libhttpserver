/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <optional>
#include <httpserver/detail/quic_datagram_sizing.hpp>
namespace httpserver::detail {
namespace {
int order(quic_packet_kind kind) {
    if (kind == quic_packet_kind::initial) return 0;
    if (kind == quic_packet_kind::handshake) return 1;
    if (kind == quic_packet_kind::one_rtt) return 2;
    return -1;
}
std::optional<std::size_t> wire_size(const quic_packet_write& h, std::size_t payload) {
    if (order(h.kind) < 0 || h.destination.size() > 20 || h.source.size() > 20 || h.token.size() > 65535 || payload > 65535 ||
        h.packet_number_width < 1 || h.packet_number_width > 4 || h.packet_number > k_quic_max_integer) return {};
    if ((h.kind != quic_packet_kind::initial && !h.token.empty()) || (h.kind == quic_packet_kind::one_rtt && !h.source.empty())) return {};
    quic_writer w;
    w.fixed(0, h.kind == quic_packet_kind::one_rtt ? 1 : 7);
    w.bytes(h.destination);
    if (h.kind != quic_packet_kind::one_rtt) w.bytes(h.source);
    if (h.kind == quic_packet_kind::initial) {
        w.varint(h.token.size(), h.token_length_width);
        w.bytes(h.token);
    }
    if (h.kind != quic_packet_kind::one_rtt) w.varint(h.packet_number_width + payload + 16, h.length_width);
    if (w.code() != quic_codec_code::ok) return {};
    return w.position() + h.packet_number_width + payload + 16;
}
}  // namespace
quic_datagram_layout size_quic_datagram(std::span<const quic_datagram_packet> packets, quic_endpoint_role role, std::size_t ceiling) {
    quic_datagram_layout result;
    if (packets.empty() || packets.size() > 3 || ceiling < 1200 || ceiling > 65535) return result;
    bool initial_minimum = false;
    int previous = -1;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto& p = packets[i];
        const auto index = order(p.header.kind);
        if (index <= previous || !std::ranges::equal(p.header.destination, packets[0].header.destination)) return result;
        previous = index;
        // The header-protection sample starts four bytes after the PN offset.
        if (p.header.packet_number_width > 4) return result;
        result.payload[i] = std::max(p.payload_bytes, 4 - p.header.packet_number_width);
        auto wire = wire_size(p.header, result.payload[i]);
        if (!wire || *wire > ceiling - result.total) return result;
        result.wire[i] = *wire;
        result.total += *wire;
        initial_minimum |= index == 0 && (role == quic_endpoint_role::client || p.ack_eliciting);
    }
    if (initial_minimum && result.total < 1200) {
        const auto i = packets.size() - 1;
        const auto prefix = result.total - result.wire[i];
        auto low = result.payload[i], high = std::size_t{1200};
        while (low < high) {
            auto middle = low + (high - low) / 2;
            auto bytes = wire_size(packets[i].header, middle);
            if (!bytes) return {};
            if (prefix + *bytes < 1200) low = middle + 1;
            else high = middle;
        }
        auto wire = wire_size(packets[i].header, low);
        if (!wire || *wire > ceiling - prefix) return {};
        result.payload[i] = low;
        result.wire[i] = *wire;
        result.total = prefix + *wire;
    }
    for (std::size_t i = 0; i < packets.size(); ++i) result.padding[i] = result.payload[i] > packets[i].payload_bytes;
    result.code = quic_codec_code::ok;
    return result;
}
}  // namespace httpserver::detail
