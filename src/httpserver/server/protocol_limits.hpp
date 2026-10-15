/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_HTTPSERVER_SERVER_PROTOCOL_LIMITS_HPP_
#define SRC_HTTPSERVER_SERVER_PROTOCOL_LIMITS_HPP_
#include <cstddef>
#include <cstdint>
#include <optional>
#include <httpserver/server/budgets.hpp>

namespace httpserver::server {

struct header_capacity_options {
    std::optional<std::size_t> max_compressed_bytes;
    std::optional<std::size_t> max_expanded_bytes;
    std::optional<std::size_t> max_fields;
};

struct http2_capacity_options {
    std::optional<std::size_t> max_concurrent_requests;
    header_capacity_options headers;
    std::optional<std::size_t> decoder_table_bytes;
    std::optional<std::size_t> encoder_table_bytes;
};

struct http3_capacity_options {
    std::optional<std::size_t> max_concurrent_requests;
    header_capacity_options headers;
    std::optional<std::size_t> decoder_table_bytes;
    std::optional<std::size_t> encoder_table_bytes;
    std::optional<std::size_t> max_blocked_headers;
    std::optional<std::size_t> max_retained_header_bytes;
};

struct quic_capacity_options {
    std::optional<std::size_t> max_stream_records;
};

struct effective_header_limits {
    std::size_t max_compressed_bytes = 0;
    std::size_t max_expanded_bytes = 0;
    std::size_t max_fields = 0;
};

struct http2_initial_advertisement {
    std::uint32_t max_concurrent_requests = 0;
    std::uint32_t decoder_table_bytes = 0;
    std::optional<std::uint32_t> max_header_list_bytes;
};

struct effective_http2_limits {
    std::size_t max_concurrent_requests = 0;
    effective_header_limits headers;
    std::size_t decoder_table_bytes = 0;
    std::size_t encoder_table_bytes = 0;
    // RFC initial allowance before the peer acknowledges local SETTINGS.
    std::size_t pre_ack_decoder_table_bytes = 4096;
    http2_initial_advertisement initial_advertisement;
};

struct http3_initial_advertisement {
    std::uint64_t decoder_table_bytes = 0;
    std::uint64_t max_blocked_headers = 0;
    std::optional<std::uint64_t> max_field_section_bytes;
};

struct effective_http3_limits {
    std::size_t max_concurrent_requests = 0;
    effective_header_limits headers;
    std::size_t decoder_table_bytes = 0;
    std::size_t encoder_table_bytes = 0;
    std::size_t max_blocked_headers = 0;
    std::size_t max_retained_header_bytes = 0;
    http3_initial_advertisement initial_advertisement;
};

struct effective_quic_limits {
    std::size_t max_stream_records = 0;
    std::uint64_t initial_max_bidirectional_streams = 0;
    std::uint64_t initial_max_unidirectional_streams = 8;
    std::size_t advertised_max_udp_payload_bytes = 1200;
    std::size_t native_send_datagram_bytes = 1200;
};

struct effective_protocol_limits {
    budget_limits budgets;
    std::optional<effective_http2_limits> http2;
    std::optional<effective_http3_limits> http3;
    std::optional<effective_quic_limits> quic;
};

}  // namespace httpserver::server

#endif  // SRC_HTTPSERVER_SERVER_PROTOCOL_LIMITS_HPP_
