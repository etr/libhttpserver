/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <limits>

#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/diagnostics_state.hpp>
namespace httpserver::detail {
void diagnostic_increment(std::atomic<std::uint64_t>& value, std::uint64_t units) noexcept {
    auto before = value.load(std::memory_order_relaxed);
    for (;;) {
        const auto after = before + std::min(units, std::numeric_limits<std::uint64_t>::max() - before);
        if (value.compare_exchange_weak(before, after, std::memory_order_relaxed)) return;
    }
}
}  // namespace httpserver::detail
namespace httpserver::server {
void diagnostics::state::record(diagnostic_event event) noexcept {
    const auto index = static_cast<std::size_t>(event.kind);
    if (index >= diagnostic_kind_count) return;
    httpserver::detail::diagnostic_increment(totals[index]);
    if (!consumer) return;
    std::unique_lock lock(mutex, std::try_to_lock);
    if (!lock.owns_lock() || count == queue_capacity) {
        httpserver::detail::diagnostic_increment(dropped);
        return;
    }
    queue[(head + count) % queue_capacity] = event;
    ++count;
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
