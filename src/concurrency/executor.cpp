/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/concurrency/executor.hpp>
#include <utility>

namespace httpserver::detail {

executor*& current_executor_slot() noexcept {
    thread_local executor* current = nullptr;
    return current;
}

}  // namespace httpserver::detail

namespace httpserver {

auto inline_executor::post(handler work) -> void {
    executor* const previous = detail::current_executor_slot();
    detail::current_executor_slot() = this;
    work();
    detail::current_executor_slot() = previous;
}

auto manual_executor::post(handler work) -> void {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(std::move(work));
        // Draining may complete and retire the owner as soon as mu_
        // is released. Finish all executor accesses before publication.
        cv_.notify_one();
    }
}

auto manual_executor::pending() const -> std::size_t {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

auto manual_executor::run_pending() -> std::size_t {
    std::size_t executed = 0;
    while (run_one()) ++executed;
    return executed;
}

auto manual_executor::run_one() -> bool {
    handler work;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.empty()) return false;
        work = std::move(queue_.front());
        queue_.pop_front();
    }
    run_work(std::move(work));
    return true;
}

auto manual_executor::run_work(handler&& work) -> void {
    const std::thread::id self = std::this_thread::get_id();
    running_id_.store(self, std::memory_order_relaxed);
    executor* const previous = detail::current_executor_slot();
    detail::current_executor_slot() = this;
    work();
    detail::current_executor_slot() = previous;
    running_id_.store(std::thread::id{}, std::memory_order_relaxed);
}

}  // namespace httpserver
