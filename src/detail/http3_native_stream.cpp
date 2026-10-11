/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <memory>
#include <utility>
#include <httpserver/detail/http3_request_state.hpp>
namespace httpserver::detail {
void http3_request_engine::state::stream::notify() const {
    if (owner.native.progress && *owner.native.progress) (*owner.native.progress)();
}
void http3_request_engine::state::stream::native_start() {
    native_lifetime = owner.shared_from_this();
    affinity = std::make_shared<http2_dispatch>(*owner.native.workers, owner.native.drain.get());
    native_handlers = std::make_shared<http3_request_helpers::route_executor>(*affinity, affinity);
    const auto weak = weak_from_this();
    affinity->on_overflow([weak] { if (auto self = weak.lock()) self->native_finish(http::outcome_code::limit_exceeded); });
    body.observe_progress([weak] { if (auto self = weak.lock()) self->notify(); });
    native_handlers->after_resume([weak] {
        if (auto self = weak.lock()) {
            self->native_done = self->handler.done();
            self->native_suspended = self->request.suspended();
            self->native_refused = self->request.disconnected() && self->request.disconnect_reason().code() == http::outcome_code::peer_refused;
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
void http3_request_engine::state::stream::native_cancel(http::outcome_code reason) {
    if (native_closed.exchange(true)) return;
    native_handlers->disable();
    affinity->post([self = shared_from_this(), reason] { self->native_finish(reason); });
}
void http3_request_engine::state::stream::native_finish(http::outcome_code reason) {
    if (native_retired.load()) return;
    auto keep = native_lifetime;
    native_closed = true;
    native_handlers->disable();
    request.disconnect(reason, "HTTP/3 native stream retired");
    body.fail(reason);
    handler.clear();
    complete_request(owner.bus(), request, lifecycle);
    native_done = true;
    native_retired = true;
    notify();
    native_lifetime.reset();
}
void http3_request_engine::state::apply_native() {
    if (!native.workers) return;
    for (auto& [id, r] : records) {
        auto value = r->semantic;
        if (!value || r->cancelled) continue;
        consumed(*r, value->body.take_consumed());
        if (value->native_abort.exchange(false)) {
            reset(*r, 0x102); continue;
        }
        if (value->native_continue.exchange(false) && !section(*r, 100, {}, false)) {
            reset(*r, 0x102); continue;
        }
        std::shared_ptr<stream::command> prepared;
        { std::lock_guard lock(value->command_mutex); prepared = std::move(value->response); }
        if (!prepared || value->native_closed.load()) continue;
        if (prepared->websocket) {
            // The wire owner commits the opening response without abandoning input.
            if (!upgrade_available(*value) || !section(*r, 200, prepared->fields, false)) {
                prepared->websocket->transport_failed({http::outcome_code::connection_closed, "WebSocket opening refused"});
                reset(*r, 0x10c); continue;
            }
            value->websocket = std::move(prepared->websocket);
            value->websocket_input_charge = std::move(prepared->websocket_input);
            value->websocket_output_charge = std::move(prepared->websocket_output);
            value->websocket_message_charge = std::move(prepared->websocket_message);
            observe_websocket(*value->websocket, id);
            pump_websocket_input(*r);
        } else {
            respond(*r, prepared->status, prepared->fields, prepared->streaming);
        }
    }
}
bool http3_request_engine::native_retired() {
    state_->reap();
    for (const auto& [id, r] : state_->records)
        if (r->semantic && state_->native.workers && !r->semantic->native_retired.load()) return false;
    return true;
}
void http3_request_engine::sent(std::uint64_t id, std::uint64_t offset, std::size_t bytes, bool fin, time_point now) {
    const auto found = state_->records.find(id);
    if (found == state_->records.end()) return;
    auto& record = *found->second;
    record.sent_offset = std::max(record.sent_offset, offset + bytes);
    record.fin_sent |= fin;
    record.write_anchor = now;
}
namespace {
using time_point = http3_request_engine::time_point;
void include_deadline(std::optional<time_point>& next, std::optional<time_point> due) {
    if (due && (!next || *due < *next)) next = due;
}
}  // namespace
std::optional<time_point> http3_request_engine::state::semantic_deadline(record& r, time_point now) {
    const auto v = r.semantic;
    if (!v) return {};
    std::optional<time_point> next;
    if (v->websocket) {
        const auto progress = v->websocket->snapshot();
        if (progress.closing && !progress.terminal && progress.closing_since)
            include_deadline(next, *progress.closing_since + limits.timeouts.ws_close);
        if (progress.output_pending && progress.output_pending_since)
            include_deadline(next, *progress.output_pending_since + limits.timeouts.write_idle);
        return next;
    }
    if (v->body.admitted() && !r.body_anchor) r.body_anchor = now;
    if (r.body_anchor && !r.receive_done && !r.abandoned) next = *r.body_anchor + limits.timeouts.body_idle;
    if (v->native_suspended.load()) {
        if (!r.suspension_anchor) r.suspension_anchor = now;
        include_deadline(next, *r.suspension_anchor + limits.timeouts.suspension);
    } else {
        r.suspension_anchor.reset();
    }
    return next;
}
std::optional<time_point> http3_request_engine::state::record_deadline(record& r, time_point now) {
    auto next = semantic_deadline(r, now);
    if (!r.accepted) include_deadline(next, r.header_deadline);
    const bool buffered = r.semantic && r.semantic->body.queued();
    const bool unsent = r.retained_offset > r.sent_offset || (r.fin_retained && !r.fin_sent);
    if (buffered || unsent || !r.sections.empty() || !r.output.empty()) {
        if (!r.write_anchor) r.write_anchor = now;
        include_deadline(next, *r.write_anchor + limits.timeouts.write_idle);
    } else {
        r.write_anchor.reset();
    }
    return next;
}
std::optional<time_point> http3_request_engine::next_deadline() {
    auto& s = *state_;
    if (!s.native.workers || s.disconnected) return {};
    const auto now = std::chrono::steady_clock::now();
    std::optional<time_point> next;
    if (!s.highest_accepted) next = s.header_anchor + s.limits.timeouts.header;
    for (auto& [id, r] : s.records) {
        if (!r->cancelled) include_deadline(next, s.record_deadline(*r, now));
    }
    return next;
}
void http3_request_engine::expire(time_point now) {
    auto& s = *state_;
    if (!s.native.workers || s.disconnected) return;
    static_cast<void>(next_deadline());
    if (!s.highest_accepted && now >= s.header_anchor + s.limits.timeouts.header) {
        s.fail(h3_error(0x102, "HTTP/3 header timeout")); return;
    }
    for (auto& [id, r] : s.records) {
        if (r->cancelled) continue;
        const auto due = s.record_deadline(*r, now);
        if (due && now >= *due) s.reset(*r, 0x10c, http::outcome_code::timeout);
    }
}
}  // namespace httpserver::detail
