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

// TASK-110: the counted drain scope. See drain_scope.hpp for the
// design contract.

#include <httpserver/detail/drain_scope.hpp>

#include <utility>

namespace httpserver {

namespace detail {

void drain_scope::enter() noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    ++active_;
}

void drain_scope::leave() noexcept {
    bool zero = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (active_ > 0) --active_;
        zero = active_ == 0;
    }
    if (zero) cv_.notify_all();
}

std::size_t drain_scope::active() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return active_;
}

void drain_scope::arm(
        std::chrono::steady_clock::time_point deadline,
        concurrency::unique_function<bool()> on_counted_thread,
        concurrency::unique_function<void()> cancel_remaining) noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    deadline_ = deadline;
    on_counted_thread_ = std::move(on_counted_thread);
    cancel_remaining_ = std::move(cancel_remaining);
    armed_ = true;
}

bool drain_scope::armed() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return armed_;
}

http::outcome drain_scope::wait(server::drain_result& out) {
    std::unique_lock<std::mutex> lock(mu_);
    if (!armed_) {
        return http::outcome(http::outcome_code::invalid_state,
                             "drain_scope: no drain armed");
    }
    if (active_ == 0) {
        out.status = server::drain_status::completed;
        out.remaining = 0;
        return http::outcome::okay();
    }
    if (on_counted_thread_ && on_counted_thread_()) {
        return http::outcome(
            http::outcome_code::would_deadlock,
            "drain_scope: wait from work counted by this drain");
    }
    for (;;) {
        cv_.wait_until(lock, deadline_);
        if (active_ == 0) {
            out.status = server::drain_status::completed;
            out.remaining = 0;
            return http::outcome::okay();
        }
        if (std::chrono::steady_clock::now() >= deadline_) break;
    }
    // Expired. Snapshot the pre-cancel count (PRD-V3N-REQ-032), claim
    // the one cancel slot under the lock, then run the hook unlocked:
    // it hard-stops engines and may take their mutexes, and a unit
    // leaving concurrently must be able to take this one.
    const std::size_t remaining = active_;
    const bool first_expiry = !cancelled_;
    cancelled_ = true;
    lock.unlock();
    if (first_expiry && cancel_remaining_) cancel_remaining_();
    out.status = server::drain_status::deadline_expired;
    out.remaining = remaining;
    return http::outcome::okay();
}

}  // namespace detail

}  // namespace httpserver
