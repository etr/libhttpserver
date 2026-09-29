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

#ifndef SRC_HTTPSERVER_CONCURRENCY_EXECUTOR_HPP_
#define SRC_HTTPSERVER_CONCURRENCY_EXECUTOR_HPP_

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace httpserver {

namespace concurrency {

// Move-only type-erasing callable. The v3 concurrency core deliberately
// does not use std::move_only_function (C++23) so the public headers
// stay C++20. Like the standard type, invoking an empty unique_function
// is undefined; callers only invoke live work items.
template<typename Signature>
class unique_function;

template<typename R, typename... Args>
class unique_function<R(Args...)> {
 public:
    unique_function() noexcept = default;

    template<typename F,
             typename = std::enable_if_t<
                 !std::is_same_v<std::decay_t<F>, unique_function>
                 && std::is_invocable_r_v<R, std::decay_t<F>&, Args...>>>
    unique_function(F&& fn)
        : fn_(std::make_unique<model<std::decay_t<F>>>(std::forward<F>(fn))) { }

    unique_function(unique_function&&) noexcept = default;
    unique_function& operator=(unique_function&&) noexcept = default;
    unique_function& operator=(std::nullptr_t) noexcept {
        fn_ = nullptr;
        return *this;
    }

    unique_function(const unique_function&) = delete;
    unique_function& operator=(const unique_function&) = delete;

    explicit operator bool() const noexcept { return fn_ != nullptr; }

    R operator()(Args... args) {
        return fn_->invoke(std::forward<Args>(args)...);
    }

 private:
    struct concept_t {
        virtual ~concept_t() = default;
        virtual R invoke(Args...) = 0;
    };

    template<typename F>
    struct model final : concept_t {
        explicit model(F fn) : fn_(std::move(fn)) { }
        R invoke(Args... args) override {
            return fn_(std::forward<Args>(args)...);
        }
        F fn_;
    };

    std::unique_ptr<concept_t> fn_;
};

}  // namespace concurrency

// Abstract scheduling seam for the v3 task system (architecture §3.1).
// A task's resumptions always run on the executor captured when the task
// is started, so completion notifications signaled from other threads are
// posted here instead of resuming coroutine frames inline.
//
// post() never blocks and never runs the work item synchronously unless
// the concrete executor documents it (inline_executor does). post() is
// expected not to throw; a throwing post() on a completion path loses the
// continuation.
class executor {
 public:
    using handler = concurrency::unique_function<void()>;

    virtual ~executor() = default;

    // Schedule `work` to run on this executor.
    virtual void post(handler work) = 0;

    // True when the calling thread is currently running this executor's
    // work (or, for inline_executor, this executor's post()).
    virtual bool is_current() const noexcept = 0;

 protected:
    executor() = default;

 private:
    executor(const executor&) = delete;
    executor& operator=(const executor&) = delete;
};

// The executor running work on the current thread while it executes
// posted work; null outside any posted-work invocation. Set by the
// shipped executors around work execution (and consequently visible
// inside spawned coroutine bodies driven by them).
namespace detail {

inline executor*& current_executor_slot() noexcept {
    thread_local executor* current = nullptr;
    return current;
}

}  // namespace detail

inline executor* current_executor() noexcept {
    return detail::current_executor_slot();
}

// Runs post() immediately on the calling thread. Not thread-safe: post()
// must be called only from one thread at a time. Useful for tests that
// want synchronous execution and for single-threaded service loops.
class inline_executor final : public executor {
 public:
    inline_executor() = default;

    void post(handler work) override {
        executor* const previous = detail::current_executor_slot();
        detail::current_executor_slot() = this;
        work();
        detail::current_executor_slot() = previous;
    }

    bool is_current() const noexcept override {
        return current_executor() == this;
    }
};

// Deterministic FIFO queue drained only by run_pending()/run_one().
// The workhorse for deterministic concurrency tests and for embedding a
// task loop into an existing single-threaded event loop. Thread-safe:
// post() may be called from any thread; work only ever runs on threads
// that call run_pending()/run_one().
class manual_executor final : public executor {
 public:
    manual_executor() = default;

    void post(handler work) override {
        {
            std::lock_guard<std::mutex> lock(mu_);
            queue_.push_back(std::move(work));
        }
        cv_.notify_one();
    }

    bool is_current() const noexcept override {
        return running_id_.load(std::memory_order_relaxed)
               == std::this_thread::get_id();
    }

    // Number of work items waiting to run.
    std::size_t pending() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queue_.size();
    }

    // Runs every pending work item, including items posted while draining.
    // Returns the number of items executed.
    std::size_t run_pending() {
        std::size_t executed = 0;
        while (run_one()) ++executed;
        return executed;
    }

    // Executes one pending work item (FIFO). Returns false when the queue
    // was empty. The current thread is marked as running this executor for
    // the duration of the work item.
    bool run_one() {
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

 private:
    void run_work(handler&& work) {
        const std::thread::id self = std::this_thread::get_id();
        running_id_.store(self, std::memory_order_relaxed);
        executor* const previous = detail::current_executor_slot();
        detail::current_executor_slot() = this;
        work();
        detail::current_executor_slot() = previous;
        running_id_.store(std::thread::id{}, std::memory_order_relaxed);
    }

    mutable std::mutex mu_;
    std::deque<handler> queue_;
    std::condition_variable cv_;
    // Tracks the thread executing a work item for is_current(). An empty
    // thread::id means no drain is in progress.
    std::atomic<std::thread::id> running_id_{};
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CONCURRENCY_EXECUTOR_HPP_
