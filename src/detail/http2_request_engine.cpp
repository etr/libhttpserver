/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <vector>
#include <httpserver/detail/http2_request_state.hpp>
namespace httpserver::detail {
namespace {
bool valid_queues(const http2_request_limits& limits) {
    return limits.body_buffer_bytes > 0 && limits.body_buffer_bytes <= 65535 &&
        limits.response_buffer_bytes > 0 && limits.response_buffer_bytes <= 65535;
}
}  // namespace
http2_request_engine::http2_request_engine(server::resource_budget budget, const server::route_registry& routes,
                                          executor& owner, http2_request_limits limits)
    : state_(std::make_unique<state>(budget, routes, owner, limits)) {
    const auto& h = limits.headers;
    if (!h.max_compressed_bytes || h.max_compressed_bytes > server::max_capacity(server::resource::body_buffer_bytes) ||
        !h.max_expanded_bytes || h.max_expanded_bytes > server::max_capacity(server::resource::header_bytes) / head_copy_allowance ||
        !h.max_fields || h.max_fields > server::max_capacity(server::resource::header_fields) || !limits.max_streams || !valid_queues(limits)) {
        state_->fail(connection_error(http2_error_code::internal_error, http::outcome_code::invalid_argument));
    }
}
http2_request_engine::~http2_request_engine() = default;
void http2_request_engine::begin_turn() {
    state_->reap(); state_->connection.begin_turn();
}
http2_feed_result http2_request_engine::feed(std::span<const std::uint8_t> bytes, http2_connection::time_point now) {
    auto result = state_->connection.feed(bytes, now);
    state_->sync_settings();
    if (state_->connection.failure()) {
        state_->reap(); return {http2_progress::failed, result.consumed, state_->connection.failure()};
    }
    if (result.error && state_->connection.header().type != 1) {
        state_->reset(result.error->stream_id, result.error->wire_code);
        state_->connection.release_frame(); return result;
    }
    if (result.error) state_->rejected = result.error->wire_code;
    if (!result.error && result.progress != http2_progress::frame_ready) return result;
    try {
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
    auto result = state_->connection.eof();
    if (result.error) {
        state_->fail(*result.error); state_->reap();
    }
    return result;
}
std::span<const std::uint8_t> http2_request_engine::output(http2_connection::time_point now) {
    state_->reap();
    if (!state_->active.empty()) return std::span(state_->active).subspan(state_->active_used);
    for (;;) {
        state_->publish_credit();
        auto control = state_->connection.output(now);
        if (!control.empty()) {
            if (!state_->control_exposed) state_->expose_credit(control);
            state_->control_exposed = true; return control;
        }
        if (state_->pending.empty()) {
            if (state_->encode_data()) return state_->active;
            state_->reap();
            if (!state_->pending.empty()) continue;
            auto terminal = state_->connection.output(now);
            state_->control_exposed = !terminal.empty(); return terminal;
        }
        if (state_->encode_next()) return state_->active;
    }
}
bool http2_request_engine::advance_output(std::size_t count) {
    if (state_->control_exposed) {
        auto n = state_->connection.output().size();
        if (!state_->connection.advance_output(count)) return false;
        if (count == n) state_->control_exposed = false;
        return true;
    }
    if (count > state_->active.size() - state_->active_used) return false;
    state_->active_used += count;
    if (state_->active_used == state_->active.size()) {
        if (auto found = state_->streams.find(state_->active_stream); found != state_->streams.end()) {
            found->second->body.retire(state_->active_body);
            if (state_->active_end) found->second->send_ended = true;
        }
        state_->active_stream = 0; state_->active_body = 0; state_->active_end = false;
        std::vector<std::uint8_t>().swap(state_->active);
        state_->active_used = 0; state_->active_charge.release();
    }
    return true;
}
const std::optional<http2_error>& http2_request_engine::failure() const { return state_->connection.failure(); }
}  // namespace httpserver::detail
