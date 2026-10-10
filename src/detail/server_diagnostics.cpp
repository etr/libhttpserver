/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <utility>
#include <httpserver/detail/diagnostics_access.hpp>

namespace {
void increment(std::atomic<std::uint64_t>& value, std::uint64_t units = 1) noexcept {
    auto before = value.load(std::memory_order_relaxed);
    for (;;) {
        const auto after = before + std::min(units, std::numeric_limits<std::uint64_t>::max() - before);
        if (value.compare_exchange_weak(before, after, std::memory_order_relaxed)) return;
    }
}
}  // namespace
namespace httpserver::server {
struct diagnostics::state {
    explicit state(callback fn) : consumer(std::move(fn)) {}
    callback consumer;
    std::array<std::atomic<std::uint64_t>, diagnostic_kind_count> totals{};
    std::array<std::atomic<std::uint64_t>, 3> gauges{};
    std::atomic<std::uint64_t> dropped{0}, failures{0};
    std::atomic_flag dispatching = ATOMIC_FLAG_INIT;
    std::mutex mutex;
    std::array<diagnostic_event, queue_capacity> queue{};
    std::size_t head = 0, count = 0;

    void record(diagnostic_event event) noexcept {
        const auto index = static_cast<std::size_t>(event.kind);
        if (index >= diagnostic_kind_count) return;
        increment(totals[index]);
        if (!consumer) return;
        std::unique_lock lock(mutex, std::try_to_lock);
        if (!lock.owns_lock() || count == queue_capacity) {
            increment(dropped);
            return;
        }
        queue[(head + count) % queue_capacity] = event;
        ++count;
    }
};
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
            increment(state->failures);
        }
    }
    return out;
}
}  // namespace httpserver::server
namespace httpserver::detail {
void diagnostics_access::record(const server::diagnostics& channel, server::diagnostic_event event) noexcept {
    if (channel.state_) channel.state_->record(event);
}
void diagnostics_access::attach(server::resource_budget& budget, const server::diagnostics& channel) noexcept {
    if (!budget.node_ || !channel.state_) return;
    budget.node_->diagnostic_owner = channel.state_;
    budget.node_->diagnostic_account = &account;
}
void diagnostics_access::account(void* opaque, server::resource kind, std::size_t units, int action) noexcept {
    auto& state = *static_cast<server::diagnostics::state*>(opaque);
    if (action < 0) {
        server::diagnostic_event event{server::diagnostic_kind::resource_refused, server::diagnostic_reason::capacity, http::outcome_code::limit_exceeded};
        event.resource_kind = kind;
        event.quantity = units;
        state.record(event);
        return;
    }
    std::size_t index;
    switch (kind) {
        case server::resource::connections:
            index = 0;
            break;
        case server::resource::streams:
            index = 1;
            break;
        case server::resource::response_queue_bytes:
            index = 2;
            break;
        default:
            return;
    }
    if (action)
        state.gauges[index].fetch_add(units, std::memory_order_relaxed);
    else
        state.gauges[index].fetch_sub(units, std::memory_order_relaxed);
}
}  // namespace httpserver::detail
