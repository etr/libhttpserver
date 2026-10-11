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
inline void append_frame(std::vector<std::uint8_t>& out, std::uint8_t type, std::uint8_t flags,
                  std::uint32_t stream, std::span<const std::uint8_t> payload) {
    const auto n = payload.size();
    out.insert(out.end(), {static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n), type, flags,
        static_cast<std::uint8_t>(stream >> 24), static_cast<std::uint8_t>(stream >> 16), static_cast<std::uint8_t>(stream >> 8), static_cast<std::uint8_t>(stream)});
    out.insert(out.end(), payload.begin(), payload.end());
}
inline std::optional<std::string_view> canonical_length(std::string_view value) {
    if (value.empty()) return {};
    for (char c : value) if (c < '0' || c > '9') return {};
    const auto first = value.find_first_not_of('0');
    return first == std::string_view::npos ? std::string_view{} : value.substr(first);
}
inline bool valid_response_lengths(const http::fields& fields, bool metadata_only) {
    std::optional<std::string_view> prior;
    for (const auto& value : fields.all("content-length")) {
        const auto number = canonical_length(value);
        if (!number) return false;
        if (!metadata_only && !number->empty()) return false;
        if (prior && *prior != *number) return false;
        prior = number;
    }
    return true;
}
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
        std::atomic<bool> native_closed{false}, native_done{false}, native_retired{false}, native_abort{false};
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
        stream(state& s, std::uint32_t n, http::request_head h, server::resource_budget scope)
            : owner(s), budget(std::move(scope)), id(n), head(std::move(h)), body(budget), lifecycle(*this, s.bus(), head, &response_refused, s.native.tls),
              request(head, &lifecycle, s.connection_id, &body, this, s.peer, s.native.tls) {}
        void on_admit(const body_policy& policy) override {
            const auto n = policy.max_buffer_bytes ? std::min<std::uint64_t>(policy.max_buffer_bytes, owner.limits.body_buffer_bytes) : owner.limits.body_buffer_bytes;
            if (!body.admit(n)) {
                if (affinity) {
                native_abort = true;
                notify();
            } else {
                    owner.reset(id, http2_error_code::refused_stream);
                }
            }
        }
        void on_respond(const http::status& status, const http::fields& fields) override {
            if (affinity) native_respond(status.code(), fields, false);
            else owner.respond(id, status.code(), fields, false);
        }
        void on_start_response(const http::status& status, const http::fields& fields) override {
            if (affinity) native_respond(status.code(), fields, true);
            else owner.respond(id, status.code(), fields, true);
        }
        websocket_upgrade_result on_upgrade(const ws_upgrade_options& options) override { return affinity ? native_upgrade(options) : owner.upgrade(*this, options); }
        bool http_trailers_allowed(bool ended) const { return !websocket && ended && !body.receive_ended() && !reset_pending; }
        void on_abort() override {
            if (affinity) {
                native_abort = true;
                notify();
            } else {
                owner.reset(id, http2_error_code::internal_error);
            }
        }
        body_push_result push(std::span<const std::byte> bytes) override {
            auto result = body.push(bytes);
            if (result.kind == body_push::failed) on_abort();
            return result;
        }
        body_push_result push_end(const http::fields& fields) override {
            auto result = body.push_end(fields);
            if (result.kind == body_push::failed) on_abort();
            return result;
        }
        const http::outcome& failure() const noexcept override { return body.failure(); }
        void park(body_write_wait& wait) override { body.park(wait); }
        void unpark(body_write_wait& wait) override { body.unpark(wait); }
        ~stream() override {
            if (affinity) return;
            owner.terminal(*this, !reset_pending && send_ended && body.receive_ended(), server::diagnostic_reason::local_stop);
            request.disconnect(http::outcome_code::connection_closed, "HTTP/2 stream ended");
            body.fail(http::outcome_code::connection_closed); handler.clear();
            complete_request(owner.bus(), request, lifecycle);
        }
    };
    struct response {
        std::uint32_t stream = 0;
        bool ended = true;
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
    std::optional<closed_kind> closed_reason(std::uint32_t id) const {
        for (const auto& entry : closed) if (entry.id == id) return entry.kind;
        return {};
    }
    void remember_closed(std::uint32_t id, closed_kind kind) {
        for (auto& entry : closed) if (entry.id == id) {
            entry.kind = kind; return;
        }
        for (std::size_t offset = 0; offset < closed.size(); ++offset) {
            const auto slot = (closed_next + offset) % closed.size();
            // Never forget a local reset: late HEADERS must still decode, and
            // DATA must still debit connection credit without a duplicate reset.
            if (closed[slot].id && closed[slot].kind == closed_kind::local_reset) continue;
            closed[slot] = {id, kind}; closed_next = (slot + 1) % closed.size(); return;
        }
        fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
    }
    void observe(server::diagnostic_kind kind, std::uint64_t stream_id = 0, http::outcome_code code = http::outcome_code::ok,
                 server::diagnostic_reason reason = server::diagnostic_reason::none, server::resource resource_kind = server::resource::streams) {
        server::diagnostic_event event{kind, reason, code};
        event.protocol = http::protocol::http_2;
        event.connection_id = connection_id;
        event.stream_id = stream_id;
        event.resource_kind = resource_kind;
        diagnostics_access::record(dispatch_context.diagnostics, event);
    }
    void terminal(stream& value, bool complete, server::diagnostic_reason reason) {
        if (!value.diagnostic_admitted || value.diagnostic_terminal) return;
        value.diagnostic_terminal = true;
        observe(complete ? server::diagnostic_kind::stream_completed : server::diagnostic_kind::stream_cancelled, value.id, complete ? http::outcome_code::ok : http::outcome_code::cancelled,
                complete ? server::diagnostic_reason::none : reason);
    }
    void flow_stall(stream& value, bool blocked) {
        if (blocked && !value.flow_blocked) observe(server::diagnostic_kind::flow_stalled, value.id, http::outcome_code::ok, server::diagnostic_reason::flow_control);
        value.flow_blocked = blocked;
    }
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
        request_dispatch_context d, std::uint64_t id, net::peer_address p, http2_native_context context)
        : native(std::move(context)), budget(b),
          routes(r),
          dispatch_context(std::move(d)),
          connection_id(id),
          peer(p),
          owner(e),
          handlers(std::make_shared<route_executor>(e)),
          limits(l),
          connection(b, l.connection, receive_settings(l.max_streams), true, native.workers != nullptr),
          scope(native.drain ? native.drain : std::make_shared<drain_scope>(dispatch_context.diagnostics)) {
        connection_counted = !native.drain;
        if (connection_counted) scope->enter();
        if (native.workers) {
            if (!reserve(server::resource::body_buffer_bytes, limits.headers.max_compressed_bytes, block_charge)) {
                fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
            } else {
                try {
                    block.reserve(limits.headers.max_compressed_bytes);
                }
                catch (const std::bad_alloc&) { fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); }
            }
        }
    }
    ~state() {
        handlers->disable();
        for (auto& [id, value] : streams) cancel_websocket(*value, {http::outcome_code::connection_closed, "HTTP/2 engine destroyed"});
        streams.clear(); leave_connection();
    }
    void leave_connection() {
        if (connection_counted) {
            connection_counted = false; scope->leave();
        }
    }
    void check_drain(http2_connection::time_point now);
    void cancel_drain();
    bool drain_output_retired() const {
        return streams.empty() && pending.empty() && active.empty() && connection.output_idle();
    }
    void retire_cancelled() {
        if (phase == drain_phase::cancelled && !handlers->running() && !websocket_pumps) {
            if (native.workers) {
                for (auto& [id, value] : streams) value->native_cancel(http::outcome_code::timeout);
                std::erase_if(streams, [](const auto& entry) { return entry.second->native_retired.load(); });
                if (streams.empty()) leave_connection();
            } else {
                streams.clear();
                leave_connection();
            }
        }
    }
    static http2_settings receive_settings(std::size_t max_streams) {
        http2_settings settings; settings.initial_window_size = 0; settings.enable_connect_protocol = 1;
        settings.max_concurrent_streams = static_cast<std::uint32_t>(std::min<std::size_t>(max_streams, 0xffffffff));
        return settings;
    }
    void discard(stream& value) {
        terminal(value, false, closed_reason(value.id) == closed_kind::peer_reset ? server::diagnostic_reason::peer_reset : server::diagnostic_reason::local_stop);
        value.reset_pending = true;
        cancel_websocket(value, {http::outcome_code::cancelled, "HTTP/2 stream reset"});
        connection.discard_stream_credit(value.id);
        value.credit_queued = 0;
        value.body.fail(http::outcome_code::connection_closed);
        consumed += value.body.unread() + value.body.consumed.exchange(0);
        value.body.discard_received();
        if (native.workers) value.native_cancel(http::outcome_code::connection_closed);
        else value.request.disconnect(http::outcome_code::connection_closed, "HTTP/2 stream reset");
        value.reset_pending = true;
    }
    bool incomplete(const stream& value) const {
        return !value.websocket && value.done() && value.response_started && !value.body.finished() && !value.send_ended && !value.reset_pending;
    }
    bool retired(stream& value) {
        if (!native.workers) return value.reset_pending || (value.done() && value.send_ended && value.body.receive_ended());
        if (!value.reset_pending && value.done() && value.send_ended && value.body.receive_ended()) value.native_cancel(http::outcome_code::connection_closed, true);
        return value.native_retired.load();
    }
    void reap() {
        if (handlers->running() || websocket_pumps) return;
        if (phase == drain_phase::cancelled) {
            retire_cancelled(); return;
        }
        if (connection.failure()) {
            if (native.workers) {
                for (auto& [id, value] : streams) value->native_cancel(connection.failure()->outcome);
                std::erase_if(streams, [](const auto& entry) { return entry.second->native_retired.load(); });
            } else {
                streams.clear();
            }
            return;
        }
        for (auto it = streams.begin(); it != streams.end();) {
            auto& value = *it->second;
            if (!value.reset_pending && value.done() && (native.workers ? value.native_refused.load() : (value.request.disconnected()
                    && value.request.disconnect_reason().code() == http::outcome_code::peer_refused))) {
                reset(value.id, http2_error_code::refused_stream);
            }
            if (incomplete(value)) {
                reset(value.id, http2_error_code::internal_error);
            }
            if (retired(value)) {
                consumed += value.body.unread() + value.body.consumed.exchange(0);
                if (!value.reset_pending) remember_closed(value.id, closed_kind::complete);
                if (connection.failure()) return;
                if (native.workers) terminal(value, !value.reset_pending && value.send_ended && value.body.receive_ended(), server::diagnostic_reason::local_stop);
                it = streams.erase(it);
            } else {
                ++it;
            }
        }
    }
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
    void clear_block() {
        if (native.workers) {
            block.clear();
        } else {
            std::vector<std::uint8_t>().swap(block);
            block_charge.release();
        }
        assembling = 0;
    }
    void fail(http2_error error) {
        if (!diagnostic_failure) {
            diagnostic_failure = true;
            observe(server::diagnostic_kind::protocol_failure, 0, error.outcome, server::diagnostic_reason::malformed_input);
        }
        handlers->disable();
        connection.terminate(error);
        ++websocket_pumps;
        for (auto& [id, value] : streams) cancel_websocket(*value, {error.outcome, "HTTP/2 connection terminated"});
        --websocket_pumps;
        clear_block(); pending.clear();
        // A sink may fail while its handler is executing. Destruction must
        // wait for the next owner pump, after that resume has returned.
        // Active compression output is immutable after exposure. Finish its
        // field block before the terminal GOAWAY, retaining the borrowed span.
    }
    bool reserve(server::resource kind, std::size_t n, server::reservation& charge) {
        return n == 0 || budget.reserve(kind, n, charge).ok();
    }
    bool queue_room() {
        // Reset-only entries do not occupy live streams. Bound pending work as
        // well as bytes, so cancellation scans never grow with attacker input.
        if (pending.size() < limits.max_streams) return true;
        fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded));
        return false;
    }
    void reset(std::uint32_t id, http2_error_code code) {
        if (connection.failure()) return;
        remember_closed(id, closed_kind::local_reset);
        if (connection.failure()) return;
        if (auto found = streams.find(id); found != streams.end() && !found->second->reset_pending) {
            discard(*found->second);
        }
        // Drop only semantic, not yet encoded responses. Encoder state changes
        // in output() when this queue reaches the wire owner.
        std::erase_if(pending, [id](const response& r) { return r.stream == id; });
        if (!queue_room()) return;
        response value; value.stream = id; value.reset = code;
        if (!reserve(server::resource::response_queue_bytes, 13 + sizeof(response) + 64, value.charge)) {
            fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); return;
        }
        pending.push_back(std::move(value));
    }
    std::optional<std::size_t> response_size(std::uint16_t status, const http::fields& fields) const;
    bool response_fields(response& value, std::uint16_t status, const http::fields& fields);
    void respond(std::uint32_t id, std::uint16_t status, const http::fields& fields, bool streaming);
    std::optional<http2_error> field_block_error(bool decoded) const {
        // RFC 9113 section 4.3 requires decompression even for discarded fields.
        if (!decoded) return connection_error(http2_error_code::compression_error);
        if (!closed_headers || closed_reason(assembling) == closed_kind::local_reset) return {};
        return connection_error(closed_reason(assembling) ? http2_error_code::stream_closed : http2_error_code::protocol_error);
    }
    std::optional<http2_error> assemble() {
        const auto h = connection.header();
        auto fragment = connection.payload();
        if (h.type == 1) {
            connect_eligible = connection.connect_protocol_at_frame_start();
            assembling = h.stream_id; end_stream = (h.flags & 1) != 0;
            if (!(assembling & 1)) return connection_error(http2_error_code::protocol_error);
            closed_headers = assembling <= last_stream && !streams.contains(assembling);
            last_stream = std::max(last_stream, assembling);
            if (!native.workers && !reserve(server::resource::body_buffer_bytes, limits.headers.max_compressed_bytes, block_charge)) {
                return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
            }
            block.reserve(limits.headers.max_compressed_bytes);
            if (h.flags & 8) {
                const auto padding = fragment.front(); fragment = fragment.subspan(1);
                fragment = fragment.first(fragment.size() - padding);
            }
            if (h.flags & 32) fragment = fragment.subspan(5);
        }
        if (fragment.size() > limits.headers.max_compressed_bytes - block.size()) {
            return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
        }
        block.insert(block.end(), fragment.begin(), fragment.end());
        if (h.flags & 4) return dispatch();
        return {};
    }
    bool discard_headers(std::uint32_t id) {
        const bool draining_opening = phase != drain_phase::running && !streams.contains(id);
        if (!closed_headers && !rejected && !draining_opening) return false;
        if (!closed_headers) reset(id, rejected.value_or(http2_error_code::refused_stream));
        closed_headers = false; rejected.reset(); return true;
    }
    std::optional<http2_error> dispatch() {
        auto section_budget = native.workers ? (streams.contains(assembling) ? streams.at(assembling)->budget : child_budget(budget)) : budget;
        resource_charge temporary_bytes(section_budget, server::resource::header_bytes);
        resource_charge temporary_fields(section_budget, server::resource::header_fields);
        auto decode_limits = limits.headers;
        const auto free_bytes = budget.capacity(server::resource::header_bytes) - budget.in_use(server::resource::header_bytes);
        decode_limits.max_expanded_bytes = std::min(decode_limits.max_expanded_bytes, free_bytes / head_copy_allowance);
        decode_limits.max_fields = std::min(decode_limits.max_fields, budget.capacity(server::resource::header_fields) - budget.in_use(server::resource::header_fields));
        if (!temporary_bytes.grow(decode_limits.max_expanded_bytes * head_copy_allowance) ||
            !temporary_fields.grow(decode_limits.max_fields)) {
            return connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
        }
        auto decoded = connection.compression().decoder().decode(block, decode_limits);
        const auto id = assembling; const auto ended = end_stream;
        const auto error = field_block_error(decoded.status.ok());
        clear_block();
        if (error) return error;
        if (discard_headers(id)) return {};
        if (streams.contains(id)) {
            trailers(id, ended, decoded.fields, std::move(temporary_bytes), std::move(temporary_fields)); return {};
        }
        http::request_head head;
        http2_connect_metadata connect;
        if (!http2_convert_request(decoded.fields, head, connect)) {
            reset(id, http2_error_code::protocol_error); return {};
        }
        std::optional<std::uint64_t> length;
        http2_content_length(head.head_fields, length);
        if (ended && length.value_or(0)) {
            reset(id, http2_error_code::protocol_error); return {};
        }
        if (!connect_allowed(id, head, connect)) return {};
        return admit(id, std::move(head), decoded.fields, ended, std::move(connect), std::move(section_budget), std::move(temporary_bytes), std::move(temporary_fields));
    }
    bool connect_allowed(std::uint32_t id, const http::request_head& head, const http2_connect_metadata& connect) {
        if (connect.protocol && !connect_eligible) {
            reset(id, http2_error_code::protocol_error); return false;
        }
        if (head.request_method.id() == http::method_id::connect && connect.protocol != "websocket") {
            reset(id, http2_error_code::refused_stream); return false;
        }
        return true;
    }
    std::optional<http2_error> admit(std::uint32_t id, http::request_head head, const std::vector<hpack_field>& fields, bool ended,
        http2_connect_metadata connect, server::resource_budget stream_budget, resource_charge bytes, resource_charge count) {
        reap();
        server::reservation count_charge;
        if (streams.size() >= limits.max_streams) {
            observe(server::diagnostic_kind::resource_refused, id, http::outcome_code::limit_exceeded, server::diagnostic_reason::capacity);
            reset(id, http2_error_code::refused_stream);
            return {};
        }
        if (!stream_budget.reserve(server::resource::streams, 1, count_charge).ok()) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        std::size_t retained = 0;
        for (const auto& field : fields) retained += field.name.size() + field.value.size() + 32;
        server::reservation metadata;
        if (!stream_budget.reserve(server::resource::body_buffer_bytes, sizeof(stream) + sizeof(http2_dispatch) + sizeof(route_executor) + 512, metadata).ok()) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        auto value = std::make_shared<stream>(*this, id, std::move(head), std::move(stream_budget));
        value->connect = std::move(connect);
        value->metadata_charge = std::move(metadata);
        value->count_charge = std::move(count_charge);
        bytes.shrink(bytes.units() - retained * head_copy_allowance);
        count.shrink(count.units() - fields.size());
        value->bytes_charge = std::move(bytes);
        value->fields_charge = std::move(count);
        std::optional<std::uint64_t> length;
        http2_content_length(value->head.head_fields, length);
        value->body.send_window.available = peer_initial;
        value->body.receive_window.available = local_initial;
        if (!value->body.prepare_receive(65535, ended, value->connect.protocol ? std::nullopt : length)) {
            reset(id, http2_error_code::refused_stream); return {};
        }
        auto* accepted = value.get();
        streams.emplace(id, std::move(value));
        accepted->diagnostic_admitted = true;
        observe(server::diagnostic_kind::stream_admitted, id);
        accepted_stream = std::max(accepted_stream, id);
        accepted->drain_unit.emplace(*scope);
        connection.processed_stream(id);
        if (native.workers) accepted->native_start();
        else accepted->handler.start(*handlers, dispatch_request(routes, bus(), dispatch_context.pages, accepted->lifecycle, accepted->request, dispatch_context.peers));
        return {};
    }
    std::optional<http2_error> admit_opening(http2_connection::time_point now) {
        const auto h = connection.header();
        if (h.type == 1 && (h.stream_id & 1) && h.stream_id > last_stream) return connection.open_stream(now);
        return {};
    }
    std::optional<http2_error> process_frame() {
        auto h = connection.header();
        if (h.type == 1 || h.type == 9) return assemble();
        if (h.type == 3) return peer_reset(h.stream_id);
        if (h.type == 0) {
            return data_frame();
        } else if (h.type == 8) {
            return update_window();
        }
        return {};
    }
    std::optional<http2_error> peer_reset(std::uint32_t id);
    bool has_borrowed_output() const { return !active.empty() || control_exposed; }
    bool encode_next();
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_STATE_HPP_
