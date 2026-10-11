/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <utility>
#include <httpserver/detail/http2_request_state.hpp>
#include <httpserver/detail/http2_websocket_handshake.hpp>
namespace httpserver::detail {
void http2_request_engine::state::stream::notify() const {
    if (owner.native.progress && *owner.native.progress) (*owner.native.progress)();
}
void http2_request_engine::state::stream::native_start() {
    native_lifetime = owner.shared_from_this();
    affinity = std::make_shared<http2_dispatch>(*owner.native.workers, owner.native.drain.get());
    native_handlers = std::make_shared<route_executor>(*affinity, affinity);
    const auto weak = weak_from_this();
    affinity->on_overflow([weak] {
        if (auto self = weak.lock()) self->native_finish(http::outcome_code::limit_exceeded, false);
    });
    body.observe_progress([weak] { if (auto self = weak.lock()) self->notify(); });
    native_handlers->after_resume([weak] {
        if (auto self = weak.lock()) {
            self->native_suspended = self->request.suspended();
            self->native_refused = self->request.disconnected() && self->request.disconnect_reason().code() == http::outcome_code::peer_refused;
            self->native_done = self->handler.done();
            self->notify();
        }
    });
    affinity->post([self = shared_from_this()] {
        if (self->native_closed.load()) return;
        auto& owner = self->owner;
        self->handler.start(*self->native_handlers, dispatch_request(owner.routes, owner.bus(), owner.dispatch_context.pages,
            self->lifecycle, self->request, owner.dispatch_context.peers));
    });
}
void http2_request_engine::state::stream::native_cancel(http::outcome_code reason, bool completed) {
    if (native_closed.exchange(true)) return;
    native_handlers->disable();
    affinity->post([self = shared_from_this(), reason, completed] { self->native_finish(reason, completed); });
}
void http2_request_engine::state::stream::native_finish(http::outcome_code reason, bool completed) {
    if (native_retired.load()) return;
    auto lifetime = native_lifetime;
    native_closed = true;
    native_handlers->disable();
    request.disconnect(reason, "HTTP/2 native stream retired");
    body.fail(reason);
    handler.clear();
    complete_request(owner.bus(), request, lifecycle);
    native_done = true;
    // The connection owner observes this publication after all handler state,
    // frames, hooks and captured application objects have retired.
    native_retired.store(true);
    notify();
    native_lifetime.reset();
}
void http2_request_engine::state::stream::native_respond(std::uint16_t status, const http::fields& fields, bool streaming) {
    response_refused = true;
    if (native_closed.load()) return;
    const auto expanded = owner.response_size(status, fields);
    const bool metadata = status == 304 || head.request_method.id() == http::method_id::head;
    const bool no_content = status >= 204 && status <= 205;
    std::optional<std::uint64_t> length;
    auto prepared = std::make_shared<head_command>();
    prepared->head.stream = id; prepared->head.ended = !streaming; prepared->streaming = streaming;
    if (!expanded || !http2_content_length(fields, length) || (!streaming && !valid_response_lengths(fields, metadata)) ||
        (no_content && length.value_or(0)) || !owner.reserve_response(prepared->head, *expanded, fields.size(), &budget) ||
        !owner.response_fields(prepared->head, status, fields) ||
        (streaming && !body.prepare_send(owner.limits.response_buffer_bytes, length, metadata || no_content))) {
        native_abort = true; notify(); return;
    }
    {
        std::lock_guard lock(command_mutex);
        if (native_closed.load() || command) return;
        command = std::move(prepared);
        response_refused = false;
    }
    notify();
}
websocket_upgrade_result http2_request_engine::state::stream::native_upgrade(const ws_upgrade_options& options) {
    websocket_upgrade_result out;
    auto plan = negotiate_http2_websocket(head, connect, options, owner.limits.headers.max_expanded_bytes, owner.limits.headers.max_fields);
    out.status = plan.status; out.rejection_status = plan.rejection_status; out.rejection_fields = std::move(plan.rejection_fields);
    if (!out.status.ok()) return out;
    if (native_closed.load() || body.receive_ended()) {
        out.status = {http::outcome_code::connection_closed, "WebSocket stream unavailable"}; return out;
    }
    auto prepared = std::make_shared<head_command>();
    prepared->head.stream = id; prepared->head.ended = false; prepared->streaming = true;
    http::fields fields;
    if (!plan.selected_subprotocol.empty()) fields.append("sec-websocket-protocol", plan.selected_subprotocol);
    const auto expanded = owner.response_size(200, fields);
    auto effective = options.limits;
    if (!expanded || !owner.reserve_response(prepared->head, *expanded, fields.size(), &budget) ||
        !owner.response_fields(prepared->head, 200, fields) || !owner.reserve_websocket(*this, effective)) {
        websocket_input_charge.release(); websocket_output_charge.release(); websocket_message_charge.release();
        out.status = {http::outcome_code::limit_exceeded, "WebSocket storage admission refused"}; return out;
    }
    prepared->websocket = std::make_shared<websocket_driver>(effective);
    auto session = prepared->websocket->take_session();
    body.admit(65535);
    {
        std::lock_guard lock(command_mutex);
        if (native_closed.load() || command) {
            out.status = {http::outcome_code::connection_closed, "WebSocket stream unavailable"}; return out;
        }
        command = std::move(prepared);
    }
    out.session.emplace(std::move(session));
    notify(); return out;
}
void http2_request_engine::state::apply_native() {
    if (!native.workers) return;
    for (auto& [id, value] : streams) {
        if (value->native_abort.exchange(false) && !value->reset_pending) reset(id, http2_error_code::internal_error);
        std::shared_ptr<head_command> prepared;
        { std::lock_guard lock(value->command_mutex); prepared = std::move(value->command); }
        if (value->websocket && !value->reset_pending) pump_websocket_input(*value);
        if (!prepared || value->reset_pending || value->native_closed.load() || connection.failure()) continue;
        if (!queue_room()) return;
        value->response_started = prepared->streaming; value->send_ended = !prepared->streaming;
        if (prepared->websocket) {
            value->websocket = std::move(prepared->websocket);
            observe_websocket(*value);
        }
        pending.push_back(std::move(prepared->head));
    }
}
}  // namespace httpserver::detail
