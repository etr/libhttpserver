/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_request_state.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_STATE_HPP_
#include <algorithm>
#include <array>
#include <atomic>
#include <coroutine>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/resource_scope.hpp>
#include <httpserver/detail/resource_charge.hpp>
#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/lifecycle_sink.hpp>
#include <httpserver/detail/http2_request_engine.hpp>
#include <httpserver/detail/http2_request_head.hpp>
#include <httpserver/detail/http2_body_stream.hpp>
#include <httpserver/detail/http2_dispatch.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/request_handler_frames.hpp>
#include <httpserver/detail/websocket_driver.hpp>
namespace httpserver::detail {
namespace http2_request_helpers {
// Bound simultaneous decoded fields, semantic fields and raw/derived targets.
constexpr std::size_t head_copy_allowance = 3;
constexpr std::size_t data_quantum = 16384, max_non_data_burst = 8;
inline http2_error connection_error(http2_error_code code, http::outcome_code outcome = http::outcome_code::protocol_error) {
    return {http2_error_scope::connection, code, 0, outcome, "HTTP/2 request engine failure"};
}
void append_frame(std::vector<std::uint8_t>& out, std::uint8_t type, std::uint8_t flags,
                  std::uint32_t stream, std::span<const std::uint8_t> payload);
std::optional<std::string_view> canonical_length(std::string_view value);
bool valid_response_lengths(const http::fields& fields, bool metadata_only);
using request_handler_frames::route_executor;
using request_handler_frames::route_task;
}  // namespace http2_request_helpers
using namespace http2_request_helpers;  // NOLINT(build/namespaces)
struct http2_request_engine::state : std::enable_shared_from_this<state> {
    struct head_command;
    struct stream final : exchange_sink, body_sink, std::enable_shared_from_this<stream> {
        state& owner;
        server::resource_budget budget;
        std::shared_ptr<state> native_lifetime;
        std::shared_ptr<http2_dispatch> affinity;
        std::shared_ptr<route_executor> native_handlers;
        std::atomic<bool> native_closed{false}, native_done{false}, native_retired{false}, native_abort{false}, native_continue{false};
        std::atomic<bool> native_suspended{false}, native_refused{false};
        std::optional<http2_connection::time_point> native_suspension_anchor, native_write_anchor;
        http2_connection::time_point native_body_anchor = std::chrono::steady_clock::now();
        bool native_body_admitted = false;
        std::mutex command_mutex;
        std::shared_ptr<head_command> command;
        void native_start();
        void native_finish(http::outcome_code reason, bool completed);
        void native_cancel(http::outcome_code reason, bool completed = false);
        void native_respond(std::uint16_t status, const http::fields& fields, bool streaming);
        websocket_upgrade_result native_upgrade(const ws_upgrade_options& options);
        void notify() const;
        bool done() const { return affinity ? native_done.load() : handler.done(); }
        std::uint32_t id;
        http::request_head head;
        http2_connect_metadata connect;
        std::shared_ptr<websocket_driver> websocket;
        server::reservation websocket_input_charge, websocket_output_charge, websocket_message_charge;
        bool websocket_input_blocked = false, websocket_pumping = false, receive_eof_delivered = false;
        std::optional<http2_connection::time_point> websocket_write_anchor;
        std::uint64_t padding_credit = 0;
        server::reservation count_charge, metadata_charge;
        resource_charge bytes_charge, fields_charge, trailer_charge, trailer_fields_charge;
        std::optional<drain_scope::unit> drain_unit;
        http2_body_stream body;
        bool response_refused = false;
        bool diagnostic_admitted = false, diagnostic_terminal = false, flow_blocked = false;
        lifecycle_sink lifecycle;
        exchange request;
        route_task handler;
        std::int64_t credit_queued = 0;
        bool response_started = false, send_ended = false, reset_pending = false, headers_sent = false;
        stream(state& s, std::uint32_t n, http::request_head h, server::resource_budget scope);
        void on_admit(const body_policy& policy) override;
        void on_respond(const http::status& status, const http::fields& fields) override;
        void on_start_response(const http::status& status, const http::fields& fields) override;
        websocket_upgrade_result on_upgrade(const ws_upgrade_options& options) override { return affinity ? native_upgrade(options) : owner.upgrade(*this, options); }
        bool http_trailers_allowed(bool ended) const { return !websocket && ended && !body.receive_ended() && !reset_pending; }
        void on_abort() override;
        body_push_result push(std::span<const std::byte> bytes) override;
        body_push_result push_end(const http::fields& fields) override;
        const http::outcome& failure() const noexcept override { return body.failure(); }
        void park(body_write_wait& wait) override { body.park(wait); }
        void unpark(body_write_wait& wait) override { body.unpark(wait); }
        ~stream() override;
    };
    struct response {
        std::uint32_t stream = 0;
        bool ended = true;
        bool informational = false;
        std::vector<hpack_field> fields;
        std::optional<http2_error_code> reset;
        server::reservation charge;
        std::size_t wire_limit = 0, framed_limit = 13;
    };
    struct head_command {
        response head;
        std::shared_ptr<websocket_driver> websocket;
        bool streaming = false;
    };
    http2_native_context native;
    void apply_native();
    std::optional<http2_connection::time_point> native_header_anchor = std::chrono::steady_clock::now(), native_drain_deadline;
    std::optional<http2_connection::time_point> native_deadline(http2_connection::time_point now);
    void expire_native(http2_connection::time_point now);
    enum class closed_kind { local_reset, peer_reset, complete };
    struct closed_stream { std::uint32_t id = 0; closed_kind kind = closed_kind::complete; };
    // Fixed history distinguishes recent peer closure from our own reset, whose
    // in-flight field sections must still update the shared compression table.
    std::array<closed_stream, 128> closed{};
    std::size_t closed_next = 0;
    std::optional<closed_kind> closed_reason(std::uint32_t id) const;
    void remember_closed(std::uint32_t id, closed_kind kind);
    void observe(server::diagnostic_kind kind, std::uint64_t stream_id = 0, http::outcome_code code = http::outcome_code::ok,
                 server::diagnostic_reason reason = server::diagnostic_reason::none, server::resource resource_kind = server::resource::streams);
    void terminal(stream& value, bool complete, server::diagnostic_reason reason);
    void flow_stall(stream& value, bool blocked);
    bool diagnostic_failure = false;
    server::resource_budget budget;
    const server::route_registry& routes;
    request_dispatch_context dispatch_context;
    server::hook_bus empty_bus;
    std::uint64_t connection_id;
    net::peer_address peer;
    const server::hook_bus& bus() const { return dispatch_context.hooks ? *dispatch_context.hooks : empty_bus; }
    executor& owner;
    std::shared_ptr<route_executor> handlers;
    http2_request_limits limits;
    http2_connection connection;
    enum class drain_phase { running, announcing, draining, finished, cancelled };
    struct drain_control { std::atomic<bool> cancel_requested{false}; };
    std::shared_ptr<drain_scope> scope;
    std::shared_ptr<drain_control> drain = std::make_shared<drain_control>();
    drain_phase phase = drain_phase::running;
    bool connection_counted = true;
    std::uint32_t accepted_stream = 0;
    std::map<std::uint32_t, std::shared_ptr<stream>> streams;
    std::deque<response> pending;
    std::vector<std::uint8_t> block, active;
    server::reservation block_charge, active_charge;
    std::uint32_t assembling = 0, last_stream = 0;
    std::size_t active_used = 0, non_data_burst = 0, control_burst = 0;
    http2_window send_window, receive_window;
    std::uint64_t consumed = 0, connection_credit_queued = 0;
    std::uint32_t peer_initial = 65535, local_initial = 65535, selected = 0, active_stream = 0;
    std::size_t active_body = 0, websocket_pumps = 0;
    std::shared_ptr<websocket_driver> active_websocket;
    bool active_end = false;
    bool connect_eligible = false, end_stream = false, control_exposed = false, closed_headers = false;
    std::optional<http2_error_code> rejected;
    state(server::resource_budget b, const server::route_registry& r, executor& e, http2_request_limits l,
        request_dispatch_context d, std::uint64_t id, net::peer_address p, http2_native_context context);
    ~state();
    void leave_connection();
    void check_drain(http2_connection::time_point now);
    void cancel_drain();
    bool drain_output_retired() const {
        return streams.empty() && pending.empty() && active.empty() && connection.output_idle();
    }
    void retire_cancelled();
    static http2_settings receive_settings(std::size_t max_streams);
    void discard(stream& value);
    bool incomplete(const stream& value) const;
    bool retired(stream& value);
    void reap();
    websocket_upgrade_result upgrade(stream& value, const ws_upgrade_options& options);
    bool admit_websocket_response(response& reply, std::optional<std::size_t> expanded, const http::fields& fields);
    void feed_websocket_ring(stream& value, websocket_driver& driver);
    void finish_websocket_input(stream& value, websocket_driver& driver);
    void check_websocket_timeout(stream& value, http2_connection::time_point now);
    void retire_websocket_output(std::size_t previous);
    void retire_active_output();
    void receive_data(stream& value, std::span<const std::uint8_t> payload, bool ended);
    bool reserve_websocket(stream& value, websocket::options& options);
    void observe_websocket(stream& value);
    void pump_websocket_input(stream& value);
    bool frame_websocket_data(stream& value);
    void cancel_websocket(stream& value, http::outcome reason);
    void check_websocket_timeouts(http2_connection::time_point now);
    void sync_settings();
    void publish_credit();
    void publish_stream_credit(stream& value);
    void expose_credit(std::span<const std::uint8_t> control);
    bool data_ready(stream& value);
    void frame_headers(const response& value, std::span<const std::uint8_t> bytes);
    bool prepare_response(stream& value, std::uint16_t status, const http::fields& fields, bool streaming);
    bool reserve_response(response& value, std::size_t expanded, std::size_t fields, const server::resource_budget* scope = nullptr);
    std::optional<http2_error> data_frame();
    std::optional<http2_error> update_window();
    bool encode_data();
    bool select_data();
    std::span<const std::uint8_t> borrowed_output(http2_connection::time_point now);
    std::span<const std::uint8_t> control_output(http2_connection::time_point now);
    bool frame_data(stream& value);
    bool encode_trailers(stream& value);
    bool trailers(std::uint32_t id, bool ended, const std::vector<hpack_field>& fields, resource_charge bytes, resource_charge count);
    void clear_block();
    void fail(http2_error error);
    bool reserve(server::resource kind, std::size_t n, server::reservation& charge) {
        return n == 0 || budget.reserve(kind, n, charge).ok();
    }
    bool queue_room();
    void reset(std::uint32_t id, http2_error_code code);
    std::optional<std::size_t> response_size(std::uint16_t status, const http::fields& fields) const;
    bool response_fields(response& value, std::uint16_t status, const http::fields& fields);
    void respond(std::uint32_t id, std::uint16_t status, const http::fields& fields, bool streaming);
    void continue_response(stream& value);
    std::optional<http2_error> field_block_error(bool decoded) const;
    std::optional<http2_error> assemble();
    bool discard_headers(std::uint32_t id);
    std::optional<http2_error> dispatch();
    bool connect_allowed(std::uint32_t id, const http::request_head& head, const http2_connect_metadata& connect);
    std::optional<http2_error> admit(std::uint32_t id, http::request_head head, const std::vector<hpack_field>& fields, bool ended,
        http2_connect_metadata connect, server::resource_budget stream_budget, resource_charge bytes, resource_charge count);
    std::optional<http2_error> admit_opening(http2_connection::time_point now);
    std::optional<http2_error> process_frame();
    std::optional<http2_error> peer_reset(std::uint32_t id);
    bool has_borrowed_output() const { return !active.empty() || control_exposed; }
    bool encode_next();
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_STATE_HPP_
