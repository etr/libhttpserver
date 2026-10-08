/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_request_state.hpp>
namespace httpserver::detail {
namespace {
bool valid_queues(const http2_request_limits& limits) {
    return limits.body_buffer_bytes > 0 && limits.body_buffer_bytes <= 65535 &&
        limits.response_buffer_bytes > 0 && limits.response_buffer_bytes <= 65535;
}
bool valid_websocket_limits(const http2_request_limits& limits) {
    const auto& ws = limits.websocket;
    return ws.validate().ok() && ws.incoming_messages <= 256 && ws.outgoing_messages <= 256 &&
        ws.incoming_bytes <= server::max_capacity(server::resource::body_buffer_bytes) &&
        ws.output_bytes <= server::max_capacity(server::resource::response_queue_bytes) && server::detail::check_timeouts(limits.timeouts).ok();
}
bool valid_request_headers(const http2_request_limits& limits) {
    const auto& h = limits.headers;
    const bool headers = h.max_compressed_bytes && h.max_compressed_bytes <= server::max_capacity(server::resource::body_buffer_bytes) &&
        h.max_expanded_bytes && h.max_expanded_bytes <= server::max_capacity(server::resource::header_bytes) / head_copy_allowance &&
        h.max_fields && h.max_fields <= server::max_capacity(server::resource::header_fields);
    return headers;
}
bool valid_request_limits(const http2_request_limits& limits) {
    return valid_request_headers(limits) && limits.max_streams && valid_queues(limits) && valid_websocket_limits(limits);
}
}  // namespace
http2_request_engine::http2_request_engine(server::resource_budget budget, const server::route_registry& routes,
                                          executor& owner, http2_request_limits limits)
    : state_(std::make_shared<state>(budget, routes, owner, limits)) {
    state_->handlers->after_resume([weak = std::weak_ptr<state>(state_)] {
        if (auto state = weak.lock()) state->retire_cancelled();
    });
    if (!valid_request_limits(limits)) state_->fail(connection_error(http2_error_code::internal_error, http::outcome_code::invalid_argument));
}
http2_request_engine::~http2_request_engine() = default;
http::outcome http2_request_engine::begin_drain(http2_connection::time_point deadline, server::drain_ticket& out) {
    if (state_->phase != state::drain_phase::running || state_->connection.failure())
        return {http::outcome_code::invalid_state, "HTTP/2 drain already initiated or terminal"};
    if (deadline <= std::chrono::steady_clock::now())
        return {http::outcome_code::invalid_argument, "HTTP/2 drain deadline elapsed"};
    auto ticket = drain_ticket_access::make(state_->scope);
    auto result = state_->connection.begin_graceful_goaway(state_->accepted_stream);
    if (result != http::outcome_code::ok) return {result, "HTTP/2 drain control reservation refused"};
    state_->phase = state::drain_phase::announcing;
    state_->scope->arm(deadline, [owner = &state_->owner, handlers = state_->handlers.get()] { return owner->is_current() || current_executor() == handlers; },
        [control = state_->drain] { control->cancel_requested.store(true); });
    ++state_->websocket_pumps;
    for (auto& [id, value] : state_->streams) if (value->websocket) value->websocket->begin_close(1001, {});
    --state_->websocket_pumps;
    out = std::move(ticket); return http::outcome::okay();
}
void http2_request_engine::state::cancel_drain() {
    if (phase != drain_phase::cancelled) {
        phase = drain_phase::cancelled;
        handlers->disable();
        for (auto& [id, value] : streams) {
            value->handler.invalidate();
            value->request.disconnect(http::outcome_code::timeout, "HTTP/2 drain deadline expired");
            value->body.fail(http::outcome_code::timeout);
        }
        fail(connection_error(http2_error_code::no_error, http::outcome_code::timeout));
    }
    retire_cancelled();
}
void http2_request_engine::state::check_drain(http2_connection::time_point now) {
    if (now == http2_connection::time_point{}) now = std::chrono::steady_clock::now();
    check_websocket_timeouts(now);
    if (phase == drain_phase::running || phase == drain_phase::finished) return;
    scope->expire_if_due(now);
    if (drain->cancel_requested.load()) {
        cancel_drain(); return;
    }
    reap();
    if (connection.graceful_complete()) phase = drain_phase::draining;
    if ((connection.graceful_complete() || connection.failure()) && drain_output_retired()) {
        leave_connection(); phase = drain_phase::finished;
    }
}
void http2_request_engine::check_drain(http2_connection::time_point now) { state_->check_drain(now); }
void http2_request_engine::begin_turn() {
    check_drain(std::chrono::steady_clock::now());
    state_->reap(); state_->connection.begin_turn();
}
http2_feed_result http2_request_engine::feed(std::span<const std::uint8_t> bytes, http2_connection::time_point now) {
    check_drain(now);
    auto result = state_->connection.feed(bytes, now);
    state_->sync_settings();
    if (state_->connection.failure()) {
        state_->fail(*state_->connection.failure());
        state_->reap(); return {http2_progress::failed, result.consumed, state_->connection.failure()};
    }
    if (result.error && state_->connection.header().type != 1) {
        state_->reset(result.error->stream_id, result.error->wire_code);
        state_->connection.release_frame(); return result;
    }
    if (result.error) state_->rejected = result.error->wire_code;
    if (!result.error && result.progress != http2_progress::frame_ready) return result;
    try {
        if (auto error = state_->admit_opening(now)) {
            state_->fail(*error); state_->reap(); return {http2_progress::failed, result.consumed, error};
        }
        auto error = state_->process_frame();
        state_->connection.release_frame();
        if (error) {
            state_->fail(*error); state_->reap();
            return {http2_progress::failed, result.consumed, error};
        }
    } catch (const std::bad_alloc&) {
        auto error = connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded);
        state_->fail(error); state_->reap(); return {http2_progress::failed, result.consumed, error};
    }
    return result;
}
http2_feed_result http2_request_engine::eof() {
    check_drain(std::chrono::steady_clock::now());
    auto result = state_->connection.eof();
    if (!result.error && (!state_->streams.empty() || state_->phase != state::drain_phase::running)) {
        auto error = connection_error(http2_error_code::no_error, http::outcome_code::connection_closed);
        result = {http2_progress::failed, 0, error};
    }
    if (result.error) {
        state_->fail(*result.error); state_->reap();
    }
    return result;
}
std::optional<http2_error> http2_request_engine::state::peer_reset(std::uint32_t id) {
    if (!(id & 1) || id > last_stream) return connection_error(http2_error_code::protocol_error);
    remember_closed(id, closed_kind::peer_reset);
    if (connection.failure()) return connection.failure();
    std::erase_if(pending, [id](const response& r) { return r.stream == id; });
    if (auto found = streams.find(id); found != streams.end()) {
        discard(*found->second);
        if (!handlers->running() && !websocket_pumps) streams.erase(found);
    }
    return {};
}
bool http2_request_engine::state::select_data() {
    if (!encode_data()) return false;
    non_data_burst = 0; return true;
}
std::span<const std::uint8_t> http2_request_engine::state::borrowed_output(http2_connection::time_point now) {
    if (!active.empty()) return std::span(active).subspan(active_used);
    if (control_exposed) return connection.output(now);
    return {};
}
std::span<const std::uint8_t> http2_request_engine::state::control_output(http2_connection::time_point now) {
    if (control_burst >= max_non_data_burst && !pending.empty()) return {};
    auto control = connection.output(now);
    if (control.empty()) return control;
    expose_credit(control); control_exposed = true;
    non_data_burst = std::min<std::size_t>(max_non_data_burst, non_data_burst + 1);
    control_burst = std::min<std::size_t>(max_non_data_burst, control_burst + 1);
    return control;
}
std::span<const std::uint8_t> http2_request_engine::output(http2_connection::time_point now) {
    check_drain(now);
    state_->reap();
    // During a reentrant close callback the final payload has retired, but
    // its frame storage still belongs to advance_output's outer invocation.
    if (state_->has_borrowed_output()) return state_->borrowed_output(now);
    for (;;) {
        state_->publish_credit();
        // Finish each exposed item (including a whole field block) before
        // rotating. Eight other items may precede an eligible DATA turn.
        // After eight controls, service the pending semantic queue as well.
        // DATA does not erase its claim, so at most one DATA turn intervenes.
        // Initial SETTINGS is necessarily among the first eight controls.
        if (state_->non_data_burst >= max_non_data_burst && state_->select_data()) return state_->active;
        auto control = state_->control_output(now);
        if (!control.empty()) return control;
        if (!state_->pending.empty()) {
            if (state_->encode_next()) {
                state_->control_burst = 0;
                state_->non_data_burst = std::min<std::size_t>(max_non_data_burst, state_->non_data_burst + 1); return state_->active;
            }
            continue;
        }
        if (state_->select_data()) return state_->active;
        state_->reap();
        if (state_->pending.empty()) return state_->control_output(now);
    }
}
bool http2_request_engine::advance_output(std::size_t count) {
    check_drain(std::chrono::steady_clock::now());
    if (state_->control_exposed) {
        auto n = state_->connection.output().size();
        if (!state_->connection.advance_output(count)) return false;
        if (count == n) state_->control_exposed = false;
        check_drain(std::chrono::steady_clock::now());
        return true;
    }
    if (count > state_->active.size() - state_->active_used) return false;
    const auto previous = state_->active_used;
    state_->active_used += count;
    state_->retire_websocket_output(previous);
    if (state_->active_used == state_->active.size()) state_->retire_active_output();
    check_drain(std::chrono::steady_clock::now());
    return true;
}
void http2_request_engine::state::retire_websocket_output(std::size_t previous) {
    auto driver = active_websocket;
    if (driver && !driver->snapshot().terminal) {
        const auto payload = [](std::size_t n) { return n > 9 ? n - 9 : 0; };
        ++websocket_pumps;
        const auto retired = payload(active_used) - payload(previous);
        driver->consume_output(retired);
        if (retired) {
            auto found = streams.find(active_stream);
            if (found != streams.end()) found->second->websocket_write_anchor = std::chrono::steady_clock::now();
        }
        --websocket_pumps;
    }
}
void http2_request_engine::state::retire_active_output() {
    if (auto found = streams.find(active_stream); found != streams.end()) {
        if (!active_websocket) found->second->body.retire(active_body);
        if (active_end) found->second->send_ended = true;
    }
    active_websocket.reset();
    active_stream = 0; active_body = 0; active_end = false;
    std::vector<std::uint8_t>().swap(active);
    active_used = 0; active_charge.release();
}
const std::optional<http2_error>& http2_request_engine::failure() const { return state_->connection.failure(); }
}  // namespace httpserver::detail
