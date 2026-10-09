/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_datagram_sizing.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_DATAGRAM_SIZING_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_DATAGRAM_SIZING_HPP_
#include <array>
#include <httpserver/detail/quic_packet.hpp>
namespace httpserver::detail {
struct quic_datagram_packet {
    quic_packet_write header;
    std::size_t payload_bytes;
    bool ack_eliciting;
};
struct quic_datagram_layout {
    quic_codec_code code = quic_codec_code::malformed;
    std::size_t total = 0;
    std::array<std::size_t, 3> payload{}, wire{};
    std::array<bool, 3> padding{};
};
quic_datagram_layout size_quic_datagram(std::span<const quic_datagram_packet> packets, quic_endpoint_role role, std::size_t ceiling);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_DATAGRAM_SIZING_HPP_
