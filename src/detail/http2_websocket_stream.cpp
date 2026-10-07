/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <memory>
#include <utility>
#include <httpserver/detail/http2_request_state.hpp>
#include <httpserver/detail/http2_websocket_handshake.hpp>
namespace httpserver::detail {
websocket_upgrade_result http2_request_engine::state::upgrade(stream& value, const ws_upgrade_options& options) {
    websocket_upgrade_result out;
    auto plan = negotiate_http2_websocket(value.head, value.connect, options, limits.headers.max_expanded_bytes, limits.headers.max_fields);
    out.status = plan.status; out.rejection_status = plan.rejection_status; out.rejection_fields = std::move(plan.rejection_fields);
    if (!out.status.ok()) return out;
    if (phase != drain_phase::running || connection.failure() || value.reset_pending || value.body.receive_ended()) {
        out.status = {http::outcome_code::connection_closed, "WebSocket stream unavailable"}; return out;
    }
    http::fields fields;
    if (!plan.selected_subprotocol.empty()) fields.append("sec-websocket-protocol", plan.selected_subprotocol);
    auto expanded = response_size(200, fields);
    response reply; reply.stream = value.id; reply.ended = false;
    if (!admit_websocket_response(reply, expanded, fields)) {
        out.status = {http::outcome_code::limit_exceeded, "WebSocket response admission refused"}; return out;
    }
    auto effective = options.limits;
    if (!reserve_websocket(value, effective)) {
        value.websocket_input_charge.release(); value.websocket_output_charge.release(); value.websocket_message_charge.release();
        out.status = {http::outcome_code::limit_exceeded, "WebSocket storage admission refused"}; return out;
    }
    auto driver = std::make_shared<websocket_driver>(effective);
    auto session = driver->take_session();
    pending.push_back(std::move(reply));
    value.websocket = std::move(driver); value.response_started = true;
    value.body.admit(65535);
    observe_websocket(value);
    out.session.emplace(std::move(session));
    return out;
}
bool http2_request_engine::state::admit_websocket_response(response& reply, std::optional<std::size_t> expanded, const http::fields& fields) {
    return expanded && pending.size() < limits.max_streams && reserve_response(reply, *expanded, fields.size()) && response_fields(reply, 200, fields);
}
bool http2_request_engine::state::reserve_websocket(stream& value, websocket::options& options) {
    const auto& cap = limits.websocket;
    options.incoming_messages = std::min(options.incoming_messages, cap.incoming_messages);
    options.outgoing_messages = std::min(options.outgoing_messages, cap.outgoing_messages);
    const auto input_fixed = 4096 + 256 * options.incoming_messages;
    // Each byte deque rounds up to a storage block independently, even for
    // empty messages. Cover two 4KiB blocks plus container/map overhead per
    // data frame, and one active control, pending Pong and pending Close.
    // The byte multiplier below covers further blocks and encoding scratch.
    const auto output_fixed = 4096 + (8192 + 256) * (options.outgoing_messages + 3);
    auto free = [&](server::resource kind) { return budget.capacity(kind) - budget.in_use(kind); };
    // Leave a separately charged DATA frame available after codec admission.
    const auto wire_headroom = data_quantum + 9 + 128;
    if (free(server::resource::body_buffer_bytes) <= input_fixed || free(server::resource::response_queue_bytes) <= output_fixed + wire_headroom + 40) return false;
    const auto input = (free(server::resource::body_buffer_bytes) - input_fixed) / 5;
    const auto output = (free(server::resource::response_queue_bytes) - output_fixed - wire_headroom) / 4;
    options.incoming_bytes = std::min({options.incoming_bytes, cap.incoming_bytes, input});
    options.output_bytes = std::min({options.output_bytes, cap.output_bytes, output});
    if (options.output_bytes <= 10) return false;
    options.max_message_bytes = std::min({options.max_message_bytes, cap.max_message_bytes,
        options.incoming_bytes, options.output_bytes - 10, free(server::resource::ws_message_bytes)});
    if (!options.max_message_bytes || !options.validate().ok()) return false;
    return reserve(server::resource::body_buffer_bytes, 5 * options.incoming_bytes + input_fixed, value.websocket_input_charge) &&
        reserve(server::resource::response_queue_bytes, 4 * options.output_bytes + output_fixed, value.websocket_output_charge) &&
        reserve(server::resource::ws_message_bytes, options.max_message_bytes, value.websocket_message_charge);
}
void http2_request_engine::state::observe_websocket(stream& value) {
    const auto id = value.id;
    auto queued = std::make_shared<std::atomic<bool>>(false);
    value.websocket->observe_progress([weak = weak_from_this(), id, queued] {
        if (queued->exchange(true)) return;
        if (auto self = weak.lock()) {
            self->owner.post([weak, id, queued] {
                queued->store(false);
                if (auto self = weak.lock()) {
                    auto found = self->streams.find(id);
                    if (found != self->streams.end()) self->pump_websocket_input(*found->second);
                }
            });
        }
    });
    owner.post([weak = weak_from_this(), id] {
        if (auto self = weak.lock()) {
            auto found = self->streams.find(id);
            if (found != self->streams.end()) self->pump_websocket_input(*found->second);
        }
    });
}
void http2_request_engine::state::pump_websocket_input(stream& value) {
    if (!value.websocket || value.reset_pending || value.websocket_pumping) return;
    value.websocket_pumping = true; ++websocket_pumps;
    struct guard {
        state& owner; stream& value;
        ~guard() { value.websocket_pumping = false; --owner.websocket_pumps; }
    } lifetime{*this, value};
    auto driver = value.websocket;
    feed_websocket_ring(value, *driver);
    finish_websocket_input(value, *driver);
}
void http2_request_engine::state::feed_websocket_ring(stream& value, websocket_driver& driver) {
    do {
        if (value.websocket_input_blocked && !driver.snapshot().input_ready) return;
        auto bytes = value.body.receive_prefix();
        if (bytes.empty() && !value.websocket_input_blocked) return;
        auto fed = driver.feed(bytes);
        value.body.consume_received(fed.consumed);
        value.websocket_input_blocked = fed.blocked && fed.status.ok();
        if (!fed.status.ok() || !fed.consumed || fed.blocked) return;
    } while (!value.reset_pending);
}
void http2_request_engine::state::finish_websocket_input(stream& value, websocket_driver& driver) {
    if (value.body.receive_ended() && !value.body.unread() && !value.websocket_input_blocked && !value.receive_eof_delivered) {
        value.receive_eof_delivered = true;
        if (!driver.snapshot().peer_close) driver.eof();
    }
    const auto progress = driver.snapshot();
    if (progress.terminal && !progress.clean && !value.reset_pending) reset(value.id, http2_error_code::cancel);
}

bool http2_request_engine::state::frame_websocket_data(stream& value) {
    const auto window = std::max<std::int64_t>(0, std::min(send_window.available, value.body.send_window.available));
    const auto maximum = std::min<std::size_t>({data_quantum, connection.peer_settings().max_frame_size, static_cast<std::size_t>(window)});
    const auto progress = value.websocket->snapshot();
    const bool end = progress.close_sent && !progress.output_pending;
    if (end) {
        if (!reserve(server::resource::response_queue_bytes, 137, active_charge)) return false;
        append_frame(active, 0, 1, value.id, {});
        active_used = 0; active_stream = selected = value.id; active_body = 0; active_end = true;
        return true;
    }
    if (!maximum || !progress.output_pending) return false;
    if (!reserve(server::resource::response_queue_bytes, maximum + 9 + 128, active_charge)) {
        fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); return false;
    }
    active.resize(maximum + 9);
    const auto n = value.websocket->copy_output(std::as_writable_bytes(std::span(active).subspan(9)));
    if (!n) {
        active.clear(); active_charge.release(); return false;
    }
    active.resize(n + 9);
    active[0] = static_cast<std::uint8_t>(n >> 16); active[1] = static_cast<std::uint8_t>(n >> 8); active[2] = static_cast<std::uint8_t>(n);
    active[3] = 0; active[4] = 0;
    for (unsigned i = 0; i < 4; ++i) active[5 + i] = static_cast<std::uint8_t>(value.id >> ((3 - i) * 8));
    send_window.debit(n); value.body.send_window.debit(n);
    active_used = 0; active_stream = selected = value.id; active_body = n; active_end = false;
    active_websocket = value.websocket;
    return true;
}
void http2_request_engine::state::cancel_websocket(stream& value, http::outcome reason) {
    if (!value.websocket) return;
    auto driver = value.websocket;
    driver->observe_progress({});
    ++websocket_pumps;
    driver->transport_failed(std::move(reason));
    --websocket_pumps;
}
void http2_request_engine::state::check_websocket_timeouts(http2_connection::time_point now) {
    if (connection.failure()) return;
    ++websocket_pumps;
    for (auto& [id, value] : streams) {
        if (!value->websocket || value->reset_pending) continue;
        pump_websocket_input(*value);
        if (value->reset_pending) continue;
        check_websocket_timeout(*value, now);
    }
    --websocket_pumps;
}
void http2_request_engine::state::check_websocket_timeout(stream& value, http2_connection::time_point now) {
    const auto progress = value.websocket->snapshot();
    if (!progress.output_pending) value.websocket_write_anchor.reset();
    else if (!value.websocket_write_anchor) value.websocket_write_anchor = progress.output_pending_since;
    const auto expired = [now](const auto& anchor, auto timeout) { return anchor && now >= *anchor && now - *anchor >= timeout; };
    if (expired(progress.closing_since, limits.timeouts.ws_close) || expired(value.websocket_write_anchor, limits.timeouts.write_idle)) {
        cancel_websocket(value, {http::outcome_code::timeout, "WebSocket deadline expired"});
        reset(value.id, http2_error_code::cancel);
    }
}
}  // namespace httpserver::detail
