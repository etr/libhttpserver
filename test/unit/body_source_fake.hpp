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

#ifndef TEST_UNIT_BODY_SOURCE_FAKE_HPP_
#define TEST_UNIT_BODY_SOURCE_FAKE_HPP_

// TASK-103: scripted engine stand-in for detail::body_source, the
// exchange body-delivery seam. It mirrors detail::recording_sink (the
// decision suites' fake of detail::exchange_sink): a bounded staging
// queue the test fills by hand, plus observables that make the
// credit-on-consumption contract visible (credit_released moves only
// inside pull(), never when bytes are merely staged).
//
// Mutex-guarded throughout: a parked read may be completed from
// another thread (the disconnect suites), so park/unpark/pull/stage
// share one lock. Completion itself goes through body_wait::complete,
// which only claims a CAS and posts to the waiter's executor — never
// runs waiter code under this lock.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver_test {

class scripted_body_source final : public httpserver::detail::body_source {
 public:
    explicit scripted_body_source(std::size_t capacity_bytes = 1u << 20)
        : capacity_(capacity_bytes) { }

    scripted_body_source(const scripted_body_source&) = delete;
    scripted_body_source& operator=(const scripted_body_source&) = delete;

    // Stages one segment at the tail of the bounded queue. Returns
    // false when the segment would exceed the capacity — the
    // engine-side backpressure boundary this fake models (a real
    // engine stops reading from the connection instead).
    bool stage(std::vector<std::byte> segment) {
        std::lock_guard<std::mutex> lock(mu_);
        if (queued_ + segment.size() > capacity_) return false;
        queued_ += segment.size();
        max_queued_ = std::max(max_queued_, queued_);
        segments_.push_back(std::move(segment));
        if (waiter_ != nullptr) complete_locked(*waiter_,
                                                httpserver::detail::body_wake::data);
        return true;
    }

    // Marks the end of the body; trailers become final for reads that
    // observe the end.
    void stage_end(httpserver::http::fields trailers = httpserver::http::fields()) {
        std::lock_guard<std::mutex> lock(mu_);
        trailers_ = std::move(trailers);
        end_ = true;
        if (waiter_ != nullptr) complete_locked(*waiter_,
                                                httpserver::detail::body_wake::end);
    }

    // Raises a framing/protocol failure; every later pull reports it
    // (the reader keeps it sticky).
    void stage_failure(httpserver::http::outcome_code code,
                       std::string detail) {
        std::lock_guard<std::mutex> lock(mu_);
        failure_.emplace(code, std::move(detail));
        if (waiter_ != nullptr) complete_locked(
            *waiter_, httpserver::detail::body_wake::failed);
    }

    // Test-side cancellation: completes a parked read as cancelled.
    void cancel() {
        std::lock_guard<std::mutex> lock(mu_);
        cancelled_ = true;
        if (waiter_ != nullptr) complete_locked(
            *waiter_, httpserver::detail::body_wake::cancelled);
    }

    // -- detail::body_source seam ----------------------------------------

    httpserver::detail::body_pull_result
    pull(std::span<std::byte> into) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (failure_.has_value()) {
            return {httpserver::detail::body_pull::failed, 0};
        }
        if (!segments_.empty() && !into.empty()) {
            std::vector<std::byte>& front = segments_.front();
            const std::size_t n = std::min(into.size(), front.size());
            std::copy_n(front.begin(), n, into.begin());
            // THE consumption event: receive credit is released for
            // exactly the bytes copied out, never for bytes merely
            // staged.
            credit_released_ += n;
            queued_ -= n;
            if (n == front.size()) {
                segments_.pop_front();
            } else {
                front.erase(front.begin(), front.begin()
                            + static_cast<std::ptrdiff_t>(n));
            }
            return {httpserver::detail::body_pull::data, n};
        }
        if (segments_.empty() && end_) {
            return {httpserver::detail::body_pull::end, 0};
        }
        return {httpserver::detail::body_pull::empty, 0};
    }

    const httpserver::http::fields& trailers() const noexcept override {
        return trailers_;
    }

    const httpserver::http::outcome& failure() const noexcept override {
        return *failure_;
    }

    void park(httpserver::detail::body_wait& wait) override {
        std::lock_guard<std::mutex> lock(mu_);
        ++park_count_;
        if (failure_.has_value()) {
            complete_locked(wait, httpserver::detail::body_wake::failed);
            return;
        }
        if (!segments_.empty()) {
            complete_locked(wait, httpserver::detail::body_wake::data);
            return;
        }
        if (end_) {
            complete_locked(wait, httpserver::detail::body_wake::end);
            return;
        }
        if (cancelled_ || wait.cancel_token().stop_requested()) {
            complete_locked(wait, httpserver::detail::body_wake::cancelled);
            return;
        }
        waiter_ = &wait;
        parked_ = true;
    }

    void unpark(httpserver::detail::body_wait& wait) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (waiter_ == &wait) {
            waiter_ = nullptr;
            parked_ = false;
        }
    }

    // -- observables ------------------------------------------------------

    std::size_t credit_released() const {
        std::lock_guard<std::mutex> lock(mu_);
        return credit_released_;
    }

    int park_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return park_count_;
    }

    bool parked() const {
        std::lock_guard<std::mutex> lock(mu_);
        return parked_;
    }

    std::size_t queued() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queued_;
    }

    std::size_t max_queued() const {
        std::lock_guard<std::mutex> lock(mu_);
        return max_queued_;
    }

 private:
    // Completes a parked waiter while holding the lock. body_wait::
    // complete only claims a CAS and posts to the waiter's executor,
    // so no waiter code runs under mu_.
    void complete_locked(httpserver::detail::body_wait& wait,
                         httpserver::detail::body_wake wake) {
        waiter_ = nullptr;
        parked_ = false;
        wait.complete(wake);
    }

    mutable std::mutex mu_;
    std::deque<std::vector<std::byte>> segments_;
    std::size_t capacity_ = 0;
    std::size_t queued_ = 0;
    std::size_t max_queued_ = 0;
    std::size_t credit_released_ = 0;
    int park_count_ = 0;
    bool parked_ = false;
    bool end_ = false;
    bool cancelled_ = false;
    std::optional<httpserver::http::outcome> failure_;
    httpserver::http::fields trailers_;
    httpserver::detail::body_wait* waiter_ = nullptr;
};

// Engine stand-in for disconnect bookkeeping. A real engine initiates
// the exchange disconnect itself, so waking its own parked body reads
// is local to it; this watcher reproduces exactly that: it awaits the
// exchange's stop fan-out and completes the fake's parked waiter as
// cancelled, exactly once. Spawn it on the handler's executor before
// the read that may park.
inline httpserver::task<void>
watch_stop_and_cancel(httpserver::exchange& x, scripted_body_source& source) {
    try {
        co_await x.cancellation().cancelled();
    } catch (const httpserver::cancelled_exception&) {
        source.cancel();
    }
}

}  // namespace httpserver_test

#endif  // TEST_UNIT_BODY_SOURCE_FAKE_HPP_
