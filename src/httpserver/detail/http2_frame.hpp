/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_frame.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_FRAME_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_FRAME_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <httpserver/server/budgets.hpp>
namespace httpserver::detail {
inline constexpr std::string_view http2_magic = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
enum class http2_error_scope { stream, connection };
enum class http2_error_code : std::uint32_t {
    protocol_error = 1, internal_error = 2, flow_control_error = 3,
    settings_timeout = 4, stream_closed = 5, frame_size_error = 6, refused_stream = 7, cancel = 8, compression_error = 9, enhance_your_calm = 11
};
enum class http2_progress { input, frame_ready, control_ready, yield, failed };
struct http2_error {
    http2_error_scope scope = http2_error_scope::connection;
    http2_error_code wire_code = http2_error_code::protocol_error;
    std::uint32_t stream_id = 0;
    http::outcome_code outcome = http::outcome_code::protocol_error;
    std::string_view diagnostic = "HTTP/2 malformed input";
};
struct http2_frame_header {
    std::uint32_t length = 0;
    std::uint8_t type = 0, flags = 0;
    std::uint32_t stream_id = 0;
};
struct http2_settings {
    std::uint32_t header_table_size = 4096, enable_push = 1;
    std::optional<std::uint32_t> max_concurrent_streams;
    std::uint32_t initial_window_size = 65535, max_frame_size = 16384;
    std::optional<std::uint32_t> max_header_list_size;
};
struct http2_feed_result {
    http2_progress progress = http2_progress::input;
    std::size_t consumed = 0;
    std::optional<http2_error> error;
};
// Owner serialized. One event remains valid until release_frame(); a feed
// with an outstanding event consumes nothing. Caller retains the input suffix.
class http2_frame_parser {
 public:
    explicit http2_frame_parser(server::resource_budget budget) : budget_(budget) {}
    http2_feed_result feed(std::span<const std::uint8_t> bytes);
    http2_feed_result eof();
    void release_frame();
    bool reserve_frame_storage(std::uint32_t maximum);
    void maximum_frame_size(std::uint32_t maximum) { maximum_ = maximum; }
    const http2_frame_header& header() const { return frame_; }
    std::span<const std::uint8_t> payload() const { return frame_.type == 8 ? std::span<const std::uint8_t>(scratch_).first(4) : std::span<const std::uint8_t>(payload_); }
    const std::array<std::uint8_t, 8>& control_payload() const { return scratch_; }
    const http2_settings& settings() const { return settings_; }
    std::uint32_t window_peak() const { return window_peak_; }
    std::optional<std::uint32_t> table_minimum() const { return table_minimum_; }
    void peer_settings(const http2_settings& settings) { peer_ = settings; }

 private:
    friend class http2_connection;
    void clear();
    http2_feed_result fail(http2_error error, std::size_t consumed);
    std::optional<http2_error> start_frame();
    std::optional<http2_error> finish_frame();
    std::optional<http2_error> header_rules();
    std::optional<http2_error> sequence_rules();
    std::optional<http2_error> retain_payload();
    bool admit_payload();
    std::optional<http2_error> payload_rules();
    std::optional<http2_error> priority_rules();
    std::optional<http2_error> take_prefix(std::span<const std::uint8_t> bytes, std::size_t& used);
    void take_payload(std::span<const std::uint8_t> bytes, std::size_t& used);
    void take_control(std::uint8_t byte);
    server::resource_budget budget_;
    server::reservation retained_, frame_storage_;
    std::vector<std::uint8_t> payload_;
    std::array<std::uint8_t, 9> header_bytes_{};
    std::array<std::uint8_t, 8> scratch_{};
    std::size_t magic_used_ = 0, header_used_ = 0, payload_used_ = 0;
    std::uint32_t window_peak_ = 65535;
    std::uint32_t maximum_ = 16384, continuation_ = 0;
    http2_frame_header frame_;
    http2_settings peer_, settings_;
    std::optional<std::uint32_t> table_minimum_;
    std::optional<http2_error> error_, deferred_;
    bool initial_ = true, ready_ = false, started_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_FRAME_HPP_
