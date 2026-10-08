/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_packet.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_PACKET_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_PACKET_HPP_
#include <optional>
#include <httpserver/detail/quic_codec.hpp>
namespace httpserver {
namespace detail {
enum class quic_packet_kind { initial, zero_rtt, handshake, retry, version_negotiation, one_rtt };
enum class quic_endpoint_role { client, server };
struct quic_packet_envelope {
    quic_packet_kind kind = quic_packet_kind::initial;
    std::uint32_t version = 1;
    std::span<const std::byte> packet, destination, source, token, versions, integrity_tag, protected_remainder;
    std::size_t packet_number_offset = 0;
};
struct quic_unmasked_fields {
    std::uint8_t first = 0;
    std::span<const std::byte> packet_number;
};
struct quic_clear_header {
    quic_packet_kind kind = quic_packet_kind::initial;
    std::uint64_t packet_number = 0;
    std::size_t packet_number_width = 1;
    std::uint8_t reserved = 0;
    bool key_phase = false;
    bool spin = false;
};
// Protection is removed externally. The reserved-bit verdict applies only
// after successful packet authentication. Views borrow the caller's datagram.
quic_decode_result<quic_packet_envelope> parse_quic_envelope(std::span<const std::byte> datagram, std::optional<std::size_t> short_cid_length = std::nullopt, quic_codec_limits limits = {}) noexcept;
quic_decode_result<quic_clear_header> decode_quic_unprotected_header(const quic_packet_envelope& envelope, quic_unmasked_fields fields, std::optional<std::uint64_t> largest_received) noexcept;
quic_codec_code validate_quic_authenticated_header(const quic_clear_header& header) noexcept;
struct quic_packet_write {
    quic_packet_kind kind = quic_packet_kind::initial;
    std::span<const std::byte> destination, source, token, payload, tag, versions;
    std::uint64_t packet_number = 0;
    std::size_t packet_number_width = 1;
    std::size_t token_length_width = 0;
    std::size_t length_width = 0;
    bool key_phase = false;
    bool spin = false;
};
// Writes the clear packet, including caller-provided opaque payload/tag.
// Output must not alias input views; failures leave output unchanged.
quic_encode_result encode_quic_packet(const quic_packet_write& packet, std::span<std::byte> output) noexcept;
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_PACKET_HPP_
