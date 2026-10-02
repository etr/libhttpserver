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

#ifndef TEST_UNIT_BODY_SINK_FAKE_HPP_
#define TEST_UNIT_BODY_SINK_FAKE_HPP_

// TASK-104: scripted engine stand-in for detail::body_sink, the
// exchange response-delivery seam. It mirrors scripted_body_source
// (the delivery suites' fake of detail::body_source): a bounded output
// queue the test drains by hand, plus observables that make the
// push-and-park contract visible (produced_ moves only inside push(),
// never while the write is parked on a full queue).
//
// Mutex-guarded throughout: a parked write may be completed from
// another thread (the disconnect suites), so park/unpark/push/drain
// share one lock. Completion itself goes through body_write_wait::
// complete, which only claims a CAS and posts to the waiter's executor
// — never runs waiter code under this lock.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/response_writer.hpp>

namespace httpserver_test {

class scripted_body_sink final : public httpserver::detail::body_sink {
 public:
    explicit scripted_body_sink(std::size_t capacity_bytes = 1u << 20)
        : capacity_(capacity_bytes) { }

    scripted_body_sink(const scripted_body_sink&) = delete;
    scripted_body_sink& operator=(const scripted_body_sink&) = delete;

    // Test-side drain: pops up to `n` bytes from the queue head (the
    // engine's progress event) and wakes a parked writer with `freed`.
    std::size_t drain(std::size_t n) {
        std::lock_guard<std::mutex> lock(mu_);
        const std::size_t popped = std::min(n, queue_.size());
        for (std::size_t i = 0; i < popped; ++i) {
            drained_log_.push_back(queue_.front());
            queue_.pop_front();
        }
        queued_ -= popped;
        drained_ += popped;
        if (popped > 0 && waiter_ != nullptr) {
            complete_locked(*waiter_, httpserver::detail::body_room::freed);
        }
        return popped;
    }

    // Raises a transport/protocol failure; every later push reports it
    // (the writer keeps it sticky) and a parked write wakes `failed`.
    void stage_failure(httpserver::http::outcome_code code,
                       std::string detail) {
        std::lock_guard<std::mutex> lock(mu_);
        failure_.emplace(code, std::move(detail));
        if (waiter_ != nullptr) {
            complete_locked(*waiter_, httpserver::detail::body_room::failed);
        }
    }

    // Test-side cancellation: completes a parked write as cancelled.
    void cancel() {
        std::lock_guard<std::mutex> lock(mu_);
        cancelled_ = true;
        if (waiter_ != nullptr) {
            complete_locked(*waiter_,
                            httpserver::detail::body_room::cancelled);
        }
    }

    // -- detail::body_sink seam ----------------------------------------

    httpserver::detail::body_push_result
    push(std::span<const std::byte> from) override {
        std::lock_guard<std::mutex> lock(mu_);
        ++push_calls_;
        if (failure_.has_value()) {
            return {httpserver::detail::body_push::failed, 0};
        }
        const std::size_t room = capacity_ - queued_;
        if (from.empty() || room == 0) {
            return {httpserver::detail::body_push::full, 0};
        }
        const std::size_t n = std::min(from.size(), room);
        // THE production event: produced_ counts only bytes actually
        // copied into the bounded queue, never parked ones.
        produced_ += n;
        queued_ += n;
        max_queued_ = std::max(max_queued_, queued_);
        for (std::size_t i = 0; i < n; ++i) queue_.push_back(from[i]);
        return {httpserver::detail::body_push::accepted, n};
    }

    httpserver::detail::body_push_result
    push_end(const httpserver::http::fields& trailers) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (failure_.has_value()) {
            return {httpserver::detail::body_push::failed, 0};
        }
        // The end marker occupies one queue slot: a full queue reports
        // full and the finish parks like a write.
        if (queued_ >= capacity_) {
            return {httpserver::detail::body_push::full, 0};
        }
        trailers_ = trailers;
        end_ = true;
        ++end_calls_;
        return {httpserver::detail::body_push::accepted, 0};
    }

    const httpserver::http::outcome& failure() const noexcept override {
        return *failure_;
    }

    void park(httpserver::detail::body_write_wait& wait) override {
        std::lock_guard<std::mutex> lock(mu_);
        ++park_count_;
        if (failure_.has_value()) {
            complete_locked(wait, httpserver::detail::body_room::failed);
            return;
        }
        // Room already free (the drain raced the park): wake at once
        // instead of parking a waiter that would never be woken.
        if (queued_ < capacity_) {
            complete_locked(wait, httpserver::detail::body_room::freed);
            return;
        }
        if (cancelled_ || wait.cancel_token().stop_requested()) {
            complete_locked(wait, httpserver::detail::body_room::cancelled);
            return;
        }
        waiter_ = &wait;
        parked_ = true;
    }

    void unpark(httpserver::detail::body_write_wait& wait) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (waiter_ == &wait) {
            waiter_ = nullptr;
            parked_ = false;
        }
    }

    // -- observables ------------------------------------------------------

    std::size_t produced() const {
        std::lock_guard<std::mutex> lock(mu_);
        return produced_;
    }

    std::size_t drained() const {
        std::lock_guard<std::mutex> lock(mu_);
        return drained_;
    }

    // The bytes drained so far, in drain order (TASK-112: the send
    // suites compare whole streamed bodies, not just counts).
    std::vector<std::byte> drained_bytes() const {
        std::lock_guard<std::mutex> lock(mu_);
        return std::vector<std::byte>(drained_log_.begin(),
                                      drained_log_.end());
    }

    std::size_t queued() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queued_;
    }

    std::size_t max_queued() const {
        std::lock_guard<std::mutex> lock(mu_);
        return max_queued_;
    }

    int park_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return park_count_;
    }

    bool parked() const {
        std::lock_guard<std::mutex> lock(mu_);
        return parked_;
    }

    bool ended() const {
        std::lock_guard<std::mutex> lock(mu_);
        return end_;
    }

    int end_calls() const {
        std::lock_guard<std::mutex> lock(mu_);
        return end_calls_;
    }

    int push_calls() const {
        std::lock_guard<std::mutex> lock(mu_);
        return push_calls_;
    }

    const httpserver::http::fields& trailers() const noexcept {
        return trailers_;
    }

 private:
    // Completes a parked waiter while holding the lock.
    // body_write_wait::complete only claims a CAS and posts to the
    // waiter's executor, so no waiter code runs under mu_.
    void complete_locked(httpserver::detail::body_write_wait& wait,
                         httpserver::detail::body_room room) {
        waiter_ = nullptr;
        parked_ = false;
        wait.complete(room);
    }

    mutable std::mutex mu_;
    std::deque<std::byte> queue_;
    std::deque<std::byte> drained_log_;
    std::size_t capacity_ = 0;
    std::size_t queued_ = 0;
    std::size_t max_queued_ = 0;
    std::size_t produced_ = 0;
    std::size_t drained_ = 0;
    int park_count_ = 0;
    bool parked_ = false;
    bool end_ = false;
    bool cancelled_ = false;
    int end_calls_ = 0;
    int push_calls_ = 0;
    std::optional<httpserver::http::outcome> failure_;
    httpserver::http::fields trailers_;
    httpserver::detail::body_write_wait* waiter_ = nullptr;
};

// Engine stand-in for disconnect bookkeeping (mirror of
// watch_stop_and_cancel): a real engine initiates the exchange
// disconnect itself, so waking its own parked response writes is local
// to it; this watcher reproduces exactly that — it awaits the
// exchange's stop fan-out and completes the fake's parked waiter as
// cancelled, exactly once. Spawn it on the handler's executor before
// the write that may park.
inline httpserver::task<void>
watch_stop_and_resume(httpserver::exchange& x, scripted_body_sink& sink) {
    try {
        co_await x.cancellation().cancelled();
    } catch (const httpserver::cancelled_exception&) {
        sink.cancel();
    }
}

}  // namespace httpserver_test

#endif  // TEST_UNIT_BODY_SINK_FAKE_HPP_
