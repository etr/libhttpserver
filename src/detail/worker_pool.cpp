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

#include <httpserver/detail/worker_pool.hpp>

#include <algorithm>
#include <utility>

namespace httpserver {

namespace detail {

std::size_t worker_pool::resolve_workers(std::size_t requested) noexcept {
    if (requested > 0) return requested;
    std::size_t detected = std::thread::hardware_concurrency();
    if (detected == 0) return 1;
    return std::min(detected, std::size_t{16});
}

worker_pool::worker_pool(std::size_t requested) {
    threads_.reserve(resolve_workers(requested));
    for (std::size_t i = 0; i < threads_.capacity(); ++i) {
        threads_.emplace_back([this] { run_worker(); });
    }
}

worker_pool::~worker_pool() {
    drain_and_join();
}

void worker_pool::post(handler work) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(std::move(work));
    }
    cv_.notify_one();
}

bool worker_pool::is_current() const noexcept {
    return current_executor() == this;
}

std::size_t worker_pool::thread_count() const noexcept {
    return threads_.size();
}

std::size_t worker_pool::pending() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

void worker_pool::drain_and_join() noexcept {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopped_ = true;
    }
    cv_.notify_all();
    for (std::thread& worker : threads_) {
        if (worker.joinable()) worker.join();
    }
    threads_.clear();
    // Inline tail: anything chained by the last running items (or
    // posted after the workers exited) still runs to completion.
    for (;;) {
        handler work;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (queue_.empty()) return;
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        run_one(work);
    }
}

void worker_pool::run_worker() noexcept {
    for (;;) {
        handler work;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] {
                return stopped_ || !queue_.empty();
            });
            if (queue_.empty()) return;
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        run_one(work);
    }
}

void worker_pool::run_one(handler& work) noexcept {
    executor* const previous = current_executor_slot();
    current_executor_slot() = this;
    work();
    current_executor_slot() = previous;
}

}  // namespace detail

}  // namespace httpserver
