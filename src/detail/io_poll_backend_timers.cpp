/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/io_timer_sweep.hpp>
#include <mutex>
#include <vector>

namespace httpserver::detail {

void io_poll_backend::expire_due_timers(std::chrono::steady_clock::time_point now) {
    // Shared with fake_io_backend (io_timer_sweep.hpp) so both drivers
    // keep identical (deadline, sequence) expiry semantics.
    sweep_due_timers(pending_, mu_, now);
}

int poll_timeout_ms(std::chrono::steady_clock::time_point now, const std::optional<std::chrono::steady_clock::time_point>& next_deadline) {
    if (!next_deadline.has_value()) {
        return k_poll_idle_cap_ms;
    }
    if (*next_deadline <= now) {
        return 0;
    }
    const std::chrono::milliseconds gap = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::ceil<std::chrono::milliseconds>(*next_deadline - now));
    if (gap.count() >= k_poll_idle_cap_ms) {
        return k_poll_idle_cap_ms;
    }
    return static_cast<int>(gap.count());
}

}  // namespace httpserver::detail
