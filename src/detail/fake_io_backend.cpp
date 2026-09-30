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

// fake_io_backend implementation (TASK-099): the pending registry,
// sequence binding, and the claim-funneled completion/cancel/close
// logic. Every terminal path erases the op from the registry and lets
// op_state::claim_terminal() decide the single winner.

#include "httpserver/detail/fake_io_backend.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "httpserver/detail/io_connection_owner.hpp"
#include "httpserver/detail/io_timer_sweep.hpp"

namespace httpserver {
namespace detail {

void fake_io_backend::submit(op_state& op) {
    std::shared_ptr<op_state> state = op.shared_from_this();
    {
        std::lock_guard<std::mutex> lock(mu_);
        op.set_sequence(next_sequence_++);
        if (closed_) {
            // Post-close submits complete immediately -- no silent drops.
            finish_now(state, io_result{http::outcome_code::connection_closed});
            return;
        }
        if (op.kind() != io_op_kind::cancel) {
            if (!pending_.emplace(&op, state).second) {
                throw std::logic_error(
                    "httpserver::io operation submitted twice to backend");
            }
            return;
        }
    }

    // A cancel op resolves at submit time from its target's state.
    const auto target =
        std::get<cancel_payload>(op.payload()).target;
    if (try_cancel(target)) {
        finish_now(state, io_result{http::outcome_code::ok});
        return;
    }
    bool closed = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        closed = closed_;
    }
    finish_now(state, io_result{closed ? http::outcome_code::connection_closed
                                       : http::outcome_code::invalid_state});
}

http::outcome_code fake_io_backend::request_cancel(op_state& target) {
    if (try_cancel(target.shared_from_this())) {
        return http::outcome_code::ok;
    }
    bool closed = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        closed = closed_;
    }
    return closed ? http::outcome_code::connection_closed
                  : http::outcome_code::invalid_state;
}

bool fake_io_backend::complete(op_state& op, io_result result) {
    std::shared_ptr<op_state> state;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = pending_.find(&op);
        if (it == pending_.end()) return false;
        state = it->second;
        pending_.erase(it);
    }
    if (!state->claim_terminal()) {
        // A concurrent cancel/close won the claim and already routed
        // the terminal result; this scripted completion is the loser.
        return false;
    }
    state->owner()->enqueue(std::move(state), result);
    return true;
}

std::size_t fake_io_backend::expire_timers(
    std::chrono::steady_clock::time_point now) {
    // Shared with io_poll_backend (io_timer_sweep.hpp) so both drivers
    // keep identical (deadline, sequence) expiry semantics.
    return sweep_due_timers(pending_, mu_, now);
}

std::size_t fake_io_backend::fire_wake() {
    std::vector<std::shared_ptr<op_state>> wakes;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->second->kind() == io_op_kind::wake) {
                wakes.push_back(it->second);
                it = pending_.erase(it);
                continue;
            }
            ++it;
        }
    }
    std::size_t fired = 0;
    for (const auto& state : wakes) {
        if (state->claim_terminal()) {
            state->owner()->enqueue(state, io_result{});
            ++fired;
        }
    }
    return fired;
}

std::size_t fake_io_backend::close() {
    std::vector<std::shared_ptr<op_state>> swept;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return 0;
        closed_ = true;
        swept.reserve(pending_.size());
        for (const auto& entry : pending_) swept.push_back(entry.second);
        pending_.clear();
    }
    std::size_t completed = 0;
    for (const auto& state : swept) {
        if (state->claim_terminal()) {
            state->owner()->enqueue(
                state, io_result{http::outcome_code::connection_closed});
            ++completed;
        }
    }
    return completed;
}

std::size_t fake_io_backend::pending_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return pending_.size();
}

bool fake_io_backend::try_cancel(const std::shared_ptr<op_state>& target) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = pending_.find(target.get());
        if (it == pending_.end()) return false;
        pending_.erase(it);
    }
    if (!target->claim_terminal()) {
        // A concurrent complete/close claimed the op first; the cancel
        // attempt lost and the caller reports invalid_state.
        return false;
    }
    target->owner()->enqueue(target, io_result{http::outcome_code::cancelled});
    return true;
}

void fake_io_backend::finish_now(const std::shared_ptr<op_state>& state,
                                 io_result result) {
    if (state->claim_terminal()) {
        state->owner()->enqueue(state, result);
    }
}

}  // namespace detail
}  // namespace httpserver
