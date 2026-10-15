/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http2_dispatch.hpp>
#include <utility>

namespace httpserver::detail {

const void*& counted_drain_slot() {
    thread_local const void* identity = nullptr;
    return identity;
}

auto http2_dispatch::post(handler work) -> void {
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

auto http2_dispatch::retire() -> void {
    std::array<handler, 16> abandoned;
    {
        std::lock_guard lock(mutex_);
        retired_ = true;
        abandoned.swap(queue_);
        count_ = 0;
    }
}

auto http2_dispatch::run_one() -> void {
    handler work;
    std::array<handler, 16> abandoned;
    {
        std::lock_guard lock(mutex_);
        if (retired_) {
            abandoned.swap(queue_);
            count_ = 0;
            work = std::move(terminal_);
        } else if (count_) {
            work = std::move(queue_[head_]);
            head_ = (head_ + 1) % queue_.size();
            --count_;
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

}  // namespace httpserver::detail
