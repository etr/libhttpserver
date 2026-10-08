/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_transport_parameters.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_TRANSPORT_PARAMETERS_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_TRANSPORT_PARAMETERS_HPP_
#include <array>
#include <optional>
#include <httpserver/detail/quic_packet.hpp>
namespace httpserver {
namespace detail {
struct quic_preferred_address {
    std::array<std::byte, 4> ipv4{};
    std::uint16_t ipv4_port = 0;
    std::array<std::byte, 16> ipv6{};
    std::uint16_t ipv6_port = 0;
    std::span<const std::byte> cid, reset_token;
};
struct quic_transport_parameters {
    std::uint64_t max_idle_timeout = 0, max_udp_payload_size = 65527, initial_max_data = 0;
    std::uint64_t initial_max_stream_data_bidi_local = 0, initial_max_stream_data_bidi_remote = 0, initial_max_stream_data_uni = 0;
    std::uint64_t initial_max_streams_bidi = 0, initial_max_streams_uni = 0;
    std::uint64_t ack_delay_exponent = 3, max_ack_delay = 25, active_connection_id_limit = 2;
    std::optional<std::span<const std::byte>> original_destination_cid, stateless_reset_token, initial_source_cid, retry_source_cid;
    std::optional<quic_preferred_address> preferred_address;
    bool disable_active_migration = false;
    std::uint32_t present = 0;
};
// The 256-entry duplicate scratch bound applies even to unknown IDs. Parsing
// validates shapes/sender restrictions; handshake CID authentication is separate.
quic_decode_result<quic_transport_parameters> decode_quic_transport_parameters(std::span<const std::byte> input, quic_endpoint_role sender, quic_codec_limits limits = {}) noexcept;
struct quic_parameter_cid_context {
    std::span<const std::byte> initial_source, original_destination;
    std::optional<std::span<const std::byte>> retry_source;
};
quic_codec_code validate_quic_transport_parameters(const quic_transport_parameters& parameters, quic_endpoint_role sender, const quic_parameter_cid_context& context) noexcept;
quic_encode_result encode_quic_transport_parameters(const quic_transport_parameters& parameters, quic_endpoint_role sender, std::span<std::byte> output, quic_codec_limits limits = {}) noexcept;
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_TRANSPORT_PARAMETERS_HPP_
