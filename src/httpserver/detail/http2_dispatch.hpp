/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_dispatch.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_DISPATCH_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_DISPATCH_HPP_
#include <array>
#include <memory>
#include <mutex>
#include <utility>
#include <httpserver/concurrency/task.hpp>
namespace httpserver::detail {
inline const void*& counted_drain_slot() { thread_local const void* identity = nullptr; return identity; }
// A bounded serial affinity. One invocation per worker turn leaves network and
// sibling-stream work eligible. Overflow has its own terminal slot; retirement
// drops queued frame witnesses outside the scheduling lock.
class http2_dispatch final : public executor, public std::enable_shared_from_this<http2_dispatch> {
 public:
    explicit http2_dispatch(executor& workers, const void* drain = nullptr) : workers_(workers), drain_(drain) {}
    void on_overflow(handler terminal) { terminal_ = std::move(terminal); }
    void post(handler work) override {
        bool schedule = false;
        {
            std::lock_guard lock(mutex_);
            if (retired_) return;
            if (count_ == queue_.size()) {
                retired_ = true;
            } else {
                queue_[(head_ + count_) % queue_.size()] = std::move(work);
                ++count_;
            }
            schedule = !scheduled_;
            scheduled_ = true;
        }
        if (schedule) workers_.post([self = shared_from_this()] { self->run_one(); });
    }
    bool is_current() const noexcept override { return current_executor() == this; }
    void retire() {
        std::array<handler, 16> abandoned;
        { std::lock_guard lock(mutex_); retired_ = true; abandoned.swap(queue_); count_ = 0; }
    }

 private:
    void run_one() {
        handler work;
        std::array<handler, 16> abandoned;
        {
            std::lock_guard lock(mutex_);
            if (retired_) {
                abandoned.swap(queue_); count_ = 0;
                work = std::move(terminal_);
            } else if (count_) {
                work = std::move(queue_[head_]); head_ = (head_ + 1) % queue_.size(); --count_;
            }
        }
        const auto previous_drain = counted_drain_slot();
        counted_drain_slot() = drain_;
        const auto previous = current_executor_slot();
        auto lifetime = std::move(current_task_executor_lifetime());
        current_executor_slot() = this;
        current_task_executor_lifetime() = shared_from_this();
        if (work) work();
        counted_drain_slot() = previous_drain;
        current_executor_slot() = previous;
        current_task_executor_lifetime() = std::move(lifetime);
        bool schedule;
        {
            std::lock_guard lock(mutex_);
            schedule = count_ || (retired_ && terminal_);
            scheduled_ = schedule;
        }
        if (schedule) workers_.post([self = shared_from_this()] { self->run_one(); });
    }
    executor& workers_;
    const void* drain_;
    std::mutex mutex_;
    std::array<handler, 16> queue_;
    handler terminal_;
    std::size_t head_ = 0, count_ = 0;
    bool scheduled_ = false, retired_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_DISPATCH_HPP_
