/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <utility>

#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/diagnostics_state.hpp>

namespace httpserver::server {
diagnostics::diagnostics(callback fn) : state_(std::make_shared<state>(std::move(fn))) {}
diagnostic_counters diagnostics::counters() const noexcept {
    auto state = state_;
    diagnostic_counters out;
    if (!state) return out;
    for (std::size_t i = 0; i < diagnostic_kind_count; ++i) out.events[i] = state->totals[i].load(std::memory_order_relaxed);
    out.active_connections = state->gauges[0].load(std::memory_order_relaxed);
    out.active_streams = state->gauges[1].load(std::memory_order_relaxed);
    out.charged_response_queue_bytes = state->gauges[2].load(std::memory_order_relaxed);
    out.dropped_events = state->dropped.load(std::memory_order_relaxed);
    out.callback_failures = state->failures.load(std::memory_order_relaxed);
    return out;
}
diagnostic_dispatch_result diagnostics::dispatch(std::size_t maximum) const noexcept {
    auto state = state_;  // Callback may destroy/replace the originating handle.
    if (!state) return {};
    if (state->dispatching.test_and_set(std::memory_order_acquire)) return {http::outcome_code::invalid_state, 0};
    struct release_dispatch {
        diagnostics::state& value;
        ~release_dispatch() { value.dispatching.clear(std::memory_order_release); }
    } guard{*state};
    std::size_t available;
    {
        std::lock_guard lock(state->mutex);
        available = std::min(maximum, state->count);
    }
    diagnostic_dispatch_result out;
    for (; out.delivered < available; ++out.delivered) {
        diagnostic_event event;
        {
            std::lock_guard lock(state->mutex);
            event = state->queue[state->head];
            state->head = (state->head + 1) % queue_capacity;
            --state->count;
        }
        try {
            state->consumer(event);
        } catch (...) {
            httpserver::detail::diagnostic_increment(state->failures);
        }
    }
    return out;
}
}  // namespace httpserver::server
