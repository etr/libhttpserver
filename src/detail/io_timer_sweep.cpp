/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_timer_sweep.hpp>
#include "./darwin_focused_diagnostics.hpp"

namespace httpserver::detail {

std::size_t sweep_due_timers(std::unordered_map<op_state*, std::shared_ptr<op_state>>& pending, std::mutex& mu, std::chrono::steady_clock::time_point now) {
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::shared_ptr<op_state>>> due;
    {
        std::lock_guard<std::mutex> lock(mu);
        for (const auto& record : pending) {
            const op_state& entry = *record.second;
            if (entry.kind() == io_op_kind::timer && std::get<timer_payload>(entry.payload()).deadline <= now) {
                due.emplace_back(std::get<timer_payload>(entry.payload()).deadline, record.second);
            }
        }
        // Vector growth is complete. Until then every timer remains pending,
        // so a collection exception can be recovered by owner shutdown.
        for (const auto& entry : due) pending.erase(entry.second.get());
    }
    std::sort(due.begin(), due.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        return lhs.second->sequence() < rhs.second->sequence();
    });
    std::size_t expired = 0;
    for (const auto& entry : due) {
        if (entry.second->claim_terminal()) {
            LHS_DARWIN_HEADER_DIAG("header timer claimed now_us=%lld sweep_now_us=%lld deadline_us=%lld id=%llu op=%p sequence=%llu\n",
                LHS_DARWIN_NOW, LHS_DARWIN_TICK(now), LHS_DARWIN_TICK(entry.first), LHS_DARWIN_ID(entry.second->connection()),
                static_cast<void*>(entry.second.get()), LHS_DARWIN_ID(entry.second->sequence()));
            entry.second->owner()->enqueue(entry.second, io_result{});
            ++expired;
        }
    }
    return expired;
}

}  // namespace httpserver::detail
