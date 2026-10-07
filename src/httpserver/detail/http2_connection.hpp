/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_connection.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_CONNECTION_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_CONNECTION_HPP_
#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <httpserver/detail/http2_frame.hpp>
#include <httpserver/detail/hpack_connection.hpp>
namespace httpserver::detail {
struct http2_limits {
    std::size_t control_frames = 64, control_bytes = 4096, pending_settings = 16, frames_per_turn = 64;
    std::chrono::steady_clock::duration settings_timeout = std::chrono::seconds(10);
    // All non-DATA frames consume control work, including ignored headers.
    // Internal fixed-window policy, independent of pump turns/queue draining.
    // Adjacent window boundaries permit up to twice the allowance in a burst.
    std::size_t control_events_per_interval = 256, stream_openings_per_interval = 128;
    std::chrono::steady_clock::duration control_interval = std::chrono::seconds(1), stream_interval = std::chrono::seconds(1);
};
// Serialized connection owner. Start each bounded pump turn with begin_turn().
// feed() exposes one stream frame until release_frame(). Output is borrowed
// until advance_output(), including after failure; an exposed frame finishes
// before terminal GOAWAY. Its first exposure commits SETTINGS and starts its
// ACK clock. No public listener dispatch, stream/window or HPACK-block handling.
class http2_connection {
 public:
    using time_point = std::chrono::steady_clock::time_point;
    explicit http2_connection(server::resource_budget budget, http2_limits limits = {}, http2_settings initial = {}, bool reserve_frames = false);
    http2_feed_result feed(std::span<const std::uint8_t> bytes, time_point now = {});
    http2_feed_result eof();
    void begin_turn() { processed_ = 0; }
    // Highest stream the exchange owner may have dispatched. GOAWAY must
    // not promise that a routed request was unprocessed.
    void processed_stream(std::uint32_t id) { last_processed_stream_ = std::max(last_processed_stream_, id); }
    // Stream engines escalate compression/admission failures through the same
    // terminal path as framing failures.
    void terminate(http2_error error) { fail(error); }
    void release_frame() { parser_.release_frame(); }
    std::optional<http2_error> open_stream(time_point now);
    const http2_frame_header& header() const { return parser_.header(); }
    std::span<const std::uint8_t> payload() const { return parser_.payload(); }
    const http2_settings& peer_settings() const { return peer_; }
    const http2_settings& local_settings() const { return local_; }
    hpack_connection& compression() { return compression_; }
    std::span<const std::uint8_t> output(time_point now = {});
    bool advance_output(std::size_t count);
    // Fixed, separately budgeted shutdown storage cannot compete with ordinary
    // controls or replace an exposed frame. A transmitted PING ACK gates stage 2.
    http::outcome_code begin_graceful_goaway(std::uint32_t cutoff);
    bool graceful_complete() const { return graceful_started_ && graceful_next_ == graceful_.size(); }
    bool output_idle() const { return !count_ && terminal_used_ == terminal_.size() &&
        (failure_ ? !graceful_exposed_ : (!graceful_started_ || graceful_complete())); }
    // Advisory limits omitted by an update retain the last advertised value.
    http::outcome_code queue_window_update(std::uint32_t stream, std::uint32_t increment);
    // Remove only unexposed receive grants; borrowed head storage stays put.
    void discard_stream_credit(std::uint32_t stream);
    std::uint32_t acknowledged_window() const { return acknowledged_window_; }
    std::uint32_t peer_window_peak() const { return parser_.window_peak(); }
    http::outcome_code queue_settings(http2_settings settings);
    std::optional<http2_error> check_timeout(time_point now);
    const std::optional<http2_error>& failure() const { return failure_; }
    std::size_t pending_settings() const { return pending_count_; }

 private:
    struct slot {
        std::array<std::uint8_t, 45> bytes{};
        std::size_t size = 0, used = 0;
        std::optional<http2_settings> settings;
        bool exposed = false;
    };
    struct pending { http2_settings settings; time_point deadline; };
    struct rate_window {
        std::optional<time_point> start;
        std::size_t used = 0;
        bool take(time_point now, std::size_t allowance, std::chrono::steady_clock::duration interval);
    };
    bool enqueue(slot value);
    bool graceful_ready() const;
    bool advance_graceful(std::size_t count);
    void preserve_borrowed_control();
    void acknowledge_barrier();
    void fail(http2_error error);
    std::optional<http2_error> control();
    bool charge_frame(const http2_feed_result& result, time_point now);
    void commit(slot& value, time_point now);
    server::resource_budget budget_;
    http2_frame_parser parser_;
    hpack_connection compression_;
    http2_limits limits_;
    rate_window control_rate_, stream_rate_;
    server::reservation control_charge_, graceful_charge_;
    std::array<slot, 3> graceful_{};
    std::size_t graceful_next_ = 0;
    std::uint32_t graceful_cutoff_ = 0;
    bool graceful_started_ = false, graceful_exposed_ = false, barrier_sent_ = false, barrier_acknowledged_ = false;
    std::array<slot, 64> slots_{};
    std::array<pending, 16> pending_{};
    std::array<std::uint8_t, 17> terminal_{};
    std::size_t head_ = 0, count_ = 0, bytes_ = 0, queued_settings_ = 0;
    std::size_t pending_head_ = 0, pending_count_ = 0, terminal_used_ = 17, processed_ = 0;
    std::uint32_t acknowledged_window_ = 65535;
    std::uint32_t last_processed_stream_ = 0;
    http2_settings peer_, local_, advertised_;
    std::optional<http2_error> failure_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_CONNECTION_HPP_
