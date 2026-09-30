/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// Shared timer-expiry sweep of the v3 I/O backends (TASK-099 fake
// driver, TASK-100 poll/WSAPoll driver). Both backends keep the same
// pending-registry shape and must expire due timers with identical
// observable semantics -- take the due ops out of the registry under
// the backend mutex, order them (deadline, then submission sequence),
// and complete each through claim_terminal() + owner enqueue outside
// the lock. The sweep lives here once so the drivers cannot drift
// apart (the backend duplication gate enforces it).
#if !defined(HTTPSERVER_COMPILATION)
#error "io_timer_sweep.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_IO_TIMER_SWEEP_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_TIMER_SWEEP_HPP_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <httpserver/detail/io_operation.hpp>

namespace httpserver {
namespace detail {

// Completes every pending timer op in @p pending whose deadline is at
// or before @p now. Takes the due entries out of the registry under
// @p mu, sorts them by (deadline, sequence), then claims and enqueues
// {ok} to each owner outside the lock (a claim loser -- cancelled or
// closed concurrently -- is a no-op). Returns how many fired.
inline std::size_t sweep_due_timers(
    std::unordered_map<op_state*, std::shared_ptr<op_state>>& pending,
    std::mutex& mu, std::chrono::steady_clock::time_point now) {
    std::vector<std::pair<std::chrono::steady_clock::time_point,
                          std::shared_ptr<op_state>>>
        due;
    {
        std::lock_guard<std::mutex> lock(mu);
        for (auto it = pending.begin(); it != pending.end();) {
            const op_state& entry = *it->second;
            if (entry.kind() == io_op_kind::timer
                && std::get<timer_payload>(entry.payload()).deadline
                       <= now) {
                due.emplace_back(
                    std::get<timer_payload>(entry.payload()).deadline,
                    it->second);
                it = pending.erase(it);
                continue;
            }
            ++it;
        }
    }
    std::sort(due.begin(), due.end(),
              [](const auto& lhs, const auto& rhs) {
                  if (lhs.first != rhs.first) {
                      return lhs.first < rhs.first;
                  }
                  return lhs.second->sequence() < rhs.second->sequence();
              });
    std::size_t expired = 0;
    for (const auto& entry : due) {
        if (entry.second->claim_terminal()) {
            entry.second->owner()->enqueue(entry.second, io_result{});
            ++expired;
        }
    }
    return expired;
}

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_IO_TIMER_SWEEP_HPP_
