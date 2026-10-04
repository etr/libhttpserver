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

namespace {
void cancel_safely(concurrency::unique_function<void()> cancel) noexcept {
    try {
        if (cancel) cancel();
    } catch (...) { }
}
}  // namespace

void drain_scope::enter() noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    ++active_;
}

void drain_scope::leave() noexcept {
    concurrency::unique_function<void()> cancel;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cancel = claim_expiry_locked(std::chrono::steady_clock::now());
        if (active_ > 0) --active_;
    }
    cv_.notify_all();
    cancel_safely(std::move(cancel));
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

concurrency::unique_function<void()> drain_scope::claim_expiry_locked(
        std::chrono::steady_clock::time_point now) noexcept {
    if (!armed_ || cancelled_ || active_ == 0 || now < deadline_) return {};
    cancelled_ = true;
    expired_remaining_ = active_;
    return std::move(cancel_remaining_);
}

bool drain_scope::expire_if_due(std::chrono::steady_clock::time_point now) noexcept {
    concurrency::unique_function<void()> cancel;
    bool expired;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cancel = claim_expiry_locked(now);
        expired = cancelled_;
    }
    cv_.notify_all();
    cancel_safely(std::move(cancel));
    return expired;
}

http::outcome drain_scope::wait(server::drain_result& out) {
    std::unique_lock<std::mutex> lock(mu_);
    if (!armed_) {
        return {http::outcome_code::invalid_state, "drain_scope: no drain armed"};
    }
    if (active_ > 0 && on_counted_thread_ && on_counted_thread_()) {
        return {http::outcome_code::would_deadlock,
                "drain_scope: wait from work counted by this drain"};
    }
    while (active_ > 0 && !cancelled_) {
        auto cancel = claim_expiry_locked(std::chrono::steady_clock::now());
        if (cancelled_) {
            lock.unlock();
            cv_.notify_all();
            cancel_safely(std::move(cancel));
            lock.lock();
            break;
        }
        cv_.wait_until(lock, deadline_);
    }
    out.status = cancelled_ ? server::drain_status::deadline_expired
                            : server::drain_status::completed;
    out.remaining = cancelled_ ? expired_remaining_ : 0;
    return http::outcome::okay();
}

}  // namespace detail

}  // namespace httpserver
