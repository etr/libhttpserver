/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_request_engine.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_ENGINE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_ENGINE_HPP_
#include <memory>
#include <httpserver/detail/http2_connection.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/server/routes.hpp>
namespace httpserver::detail {
struct http2_request_limits {
    hpack_section_limits headers{65536, 65536, 256};
    // Bounds live streams and pending output items, including reset-only items.
    std::size_t max_streams = 128;
    std::size_t body_buffer_bytes = 16384, response_buffer_bytes = 16384;
};
// Internal streaming composition seam. The supplied executor must serialize
// handlers with all engine calls in the connection owner's execution domain.
// Routes and executor outlive the engine. Engine destruction invalidates queued
// and parked handler frames; no detached task borrows exchange storage.
// feed() consumes one frame/control event and owns its release. Non-END_STREAM
// heads dispatch immediately. CONNECT and upgrades are unsupported here.
// The public listener still dispatches HTTP/1; negotiated h2 wiring is separate.
class http2_request_engine {
 public:
    http2_request_engine(server::resource_budget budget, const server::route_registry& routes,
                        executor& owner, http2_request_limits limits = {});
    ~http2_request_engine();
    void begin_turn();
    http2_feed_result feed(std::span<const std::uint8_t> bytes, http2_connection::time_point now = {});
    http2_feed_result eof();
    std::span<const std::uint8_t> output(http2_connection::time_point now = {});
    bool advance_output(std::size_t count);
    const std::optional<http2_error>& failure() const;
 private:
    struct state;
    std::unique_ptr<state> state_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_ENGINE_HPP_
