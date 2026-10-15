/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http3_request_engine.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_ENGINE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_ENGINE_HPP_
#include <chrono>
#include <memory>
#include <httpserver/server/server.hpp>
#include <httpserver/detail/http3_connection.hpp>
#include <httpserver/detail/http2_dispatch.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/detail/request_lifecycle.hpp>
namespace httpserver::detail {
struct http3_native_context {
    executor* workers = nullptr;
    std::shared_ptr<const server::tls_peer_metadata> tls;
    std::shared_ptr<drain_scope> drain;
    std::shared_ptr<executor::handler> progress;
};
struct http3_request_limits {
    server::timeout_options timeouts;
    http3_limits framing;
    websocket::options websocket{16384, 65536, 65536, 16, 16};
    std::size_t max_active_requests = 128, body_buffer_bytes = 16384, response_buffer_bytes = 16384;
    std::size_t max_receipt_records = 64, max_pending_output_records = 128;
};
struct http3_request_action {
    std::optional<quic_stop_sending_frame> stop_sending;
    std::optional<quic_reset_stream_frame> reset;
};
// Owner-serialized private composition. Transport streams, flow/recovery,
// routes and executor outlive this adapter. Extraction is never consumption.
// pump_output only retains copied reliable data; the QUIC owner schedules,
// rechecks credit, emits and records actual sent offsets. Owner forwards only
// this adapter's information completions; the adapter never drains recovery.
class http3_request_engine final {
 public:
    http3_request_engine(quic_storage_lease data, quic_storage_lease critical, quic_flow_control& flow, quic_recovery& recovery, const server::route_registry& routes,
                         executor& owner, http3_request_limits limits = {}, std::uint64_t connection_id = 0, net::peer_address peer = {},
                         request_dispatch_context dispatch = {}, http3_native_context native = {});
    ~http3_request_engine();
    static std::size_t record_storage(const http3_request_limits& limits, bool local);
    static std::size_t semantic_storage(const http3_request_limits& limits, std::size_t expanded, std::size_t fields);
    http3_request_engine(const http3_request_engine&) = delete;
    http3_request_engine& operator=(const http3_request_engine&) = delete;
    std::optional<http3_error> attach_peer(quic_stream_state& opened);
    std::optional<http3_error> attach_local(http3_role role, quic_stream_state& opened);
    http3_progress pump_receive(std::uint64_t id);
    http3_progress pump_output();
    void terminal(std::uint64_t id, quic_stream_terminal terminal);
    void stop_sending(std::uint64_t id, std::uint64_t error);
    void information_completed(const quic_information_completion& completion);
    std::optional<http3_request_action> take_action();
    void disconnect(http::outcome reason);
    // Existing exchanges keep their by-value peer snapshot.
    void set_peer_for_future_requests(net::peer_address peer);
    using time_point = std::chrono::steady_clock::time_point;
    http::outcome begin_drain(time_point deadline, server::drain_ticket& out, time_point now = std::chrono::steady_clock::now());
    void check_drain(time_point now);
    std::optional<time_point> drain_deadline() const;
    std::optional<quic_close_frame> take_connection_close();
    void begin_turn();
    std::optional<time_point> next_deadline();
    void expire(time_point now);
    void sent(std::uint64_t stream, std::uint64_t offset, std::size_t bytes, bool fin, time_point now);
    bool native_retired();
    const std::optional<http3_error>& failure() const;

 private:
    struct state;
    std::shared_ptr<state> state_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_ENGINE_HPP_
