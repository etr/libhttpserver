/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <new>
#include <utility>
#include <httpserver/detail/http3_request_state.hpp>
namespace httpserver::detail {
http::outcome http3_request_engine::begin_drain(time_point deadline, server::drain_ticket& out, time_point now) {
    auto s = state_;
    state::pump_guard guard(*s);
    if (s->disconnected || s->phase != state::drain_phase::running)
        return {http::outcome_code::invalid_state, "HTTP/3 drain already initiated or terminal"};
    if (deadline <= now) return {http::outcome_code::invalid_argument, "HTTP/3 drain deadline elapsed"};
    if (s->highest_accepted == k_quic_max_integer - 3)
        return {http::outcome_code::limit_exceeded, "HTTP/3 request ID space exhausted"};
    try {
        auto ticket = drain_ticket_access::make(s->scope);
        concurrency::unique_function<bool()> counted = [owner = &s->owner, handlers = s->handlers] {
            return owner->is_current() || current_executor() == handlers.get();
        };
        concurrency::unique_function<void()> cancel = [control = s->drain] { control->cancel_requested.store(true); };
        const auto cutoff = s->highest_accepted ? *s->highest_accepted + 4 : 0;
        auto result = s->connection.begin_goaway(cutoff);
        if (!result.ok()) return result;
        s->phase = state::drain_phase::draining;
        s->deadline = deadline;
        s->scope->arm(deadline, std::move(counted), std::move(cancel));
        s->drain_requests();
        out = std::move(ticket);
        return http::outcome::okay();
    } catch (const std::bad_alloc&) {
        return {http::outcome_code::limit_exceeded, "HTTP/3 drain allocation refused"};
    }
}
void http3_request_engine::state::drain_requests() {
    for (auto& [id, r] : records) {
        if (!r->local && !(id & 2) && !r->accepted) reset(*r, 0x10b);
        if (!r->cancelled && r->semantic && r->semantic->websocket) {
            auto driver = r->semantic->websocket;
            driver->begin_close(1001, {});
        }
    }
}
void http3_request_engine::state::leave_connection() {
    if (connection_counted) {
        connection_counted = false;
        scope->leave();
    }
}
void http3_request_engine::state::retire_drain_request(record& r) {
    if (r.drain_unit && !r.semantic && r.fin_retained && r.information.empty()) r.drain_unit.reset();
}
void http3_request_engine::state::interrupt_drain() {
    if (phase != drain_phase::draining) return;
    // Fix an incomplete report before teardown drops counted requests.
    scope->expire_if_due(*deadline);
    phase = drain_phase::finished;
    leave_connection();
}
void http3_request_engine::state::finish_drain() {
    phase = drain_phase::finished;
    close_request = quic_close_frame{true, 0x100, 0, {}};
    leave_connection();
}
void http3_request_engine::state::check_drain(time_point now) {
    if (phase != drain_phase::draining) return;
    scope->expire_if_due(now);
    if (drain->cancel_requested.load()) {
        // Owner-side cancellation only; ticket waiters merely set the atomic.
        phase = drain_phase::finished;
        handlers->disable();
        disconnected = true;
        for (auto& [id, r] : records) cancel(*r, http::outcome_code::timeout);
        connection.shutdown();
        close_request = quic_close_frame{true, 0x100, 0, {}};
        leave_connection();
        return;
    }
    const bool critical_retired = retained_critical == 0 && connection.local_prefix(http3_role::qpack_encoder).empty() &&
                                  connection.local_prefix(http3_role::qpack_decoder).empty();
    if (!drain_failed && scope->active() == 1 && connection.goaway_retired() && critical_retired) finish_drain();
}
void http3_request_engine::check_drain(time_point now) {
    auto s = state_;
    state::pump_guard guard(*s);
    s->check_drain(now);
}
std::optional<http3_request_engine::time_point> http3_request_engine::drain_deadline() const {
    return state_->phase == state::drain_phase::draining ? state_->deadline : std::nullopt;
}
std::optional<quic_close_frame> http3_request_engine::take_connection_close() {
    return std::exchange(state_->close_request, {});
}
}  // namespace httpserver::detail
