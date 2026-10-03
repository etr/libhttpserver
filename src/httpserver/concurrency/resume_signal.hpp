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

#ifndef SRC_HTTPSERVER_CONCURRENCY_RESUME_SIGNAL_HPP_
#define SRC_HTTPSERVER_CONCURRENCY_RESUME_SIGNAL_HPP_

// Application resume signal of the v3 concurrency core (architecture
// §3.1, PRD-V3N-REQ-024/025): a one-shot, idempotent, thread-safe event
// an exchange uses to wake suspended application work. The first of
// {signal(), cancel()} wins; later triggers are no-ops, so a signal that
// races a disconnect (exchange ended -> cancel()) delivers exactly one
// outcome per waiter. Deadlines apply while suspended via wait_for().
//
// Waiters are always resumed on their own frame's executor (posted,
// never inline on the signaling thread).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/concurrency/task.hpp>

namespace httpserver {

// Typed result of a completed resume wait.
enum class resume_outcome : std::uint8_t {
    resumed,   // signal() fired first
    timeout,   // the wait_for deadline expired first
    cancelled,  // cancel() fired first (e.g. the exchange ended)
};

namespace detail {

// Shared state of one resume event. trigger resolves once, atomically:
// 0 = pending, 1 = resumed, 2 = cancelled.
struct resume_state {
    std::mutex mu;                          // guards head
    std::atomic<int> trigger{0};
    std::shared_ptr<struct resume_node> head;
};

// One suspended waiter. Owned by shared_ptr (awaiter + whichever trigger
// pops it), so a waiter destroyed while pending cannot be resurrected by
// an in-flight trigger.
struct resume_node {
    std::atomic<bool> delivered{false};
    resume_outcome result = resume_outcome::resumed;
    std::coroutine_handle<> awaiting;
    std::shared_ptr<frame_witness> witness;  // null for foreign coroutines
    executor* target = nullptr;              // null resumes inline
    std::shared_ptr<resume_node> next;       // guarded by resume_state::mu

    bool claim() noexcept {
        bool expected = false;
        return delivered.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
    }

    void resume_posted() noexcept {
        if (target != nullptr) {
            try {
                target->post([witness = witness, frame = awaiting] {
                    if (witness) {
                        guarded_resume(witness, frame);
                    } else {
                        frame.resume();
                    }
                });
            } catch (...) {
            }
        } else if (witness) {
            guarded_resume(witness, awaiting);
        } else {
            awaiting.resume();
        }
    }
};

inline void deliver(const std::shared_ptr<resume_node>& node,
                    resume_outcome outcome) noexcept {
    if (!node->claim()) return;  // exactly-once: the CAS loser is a no-op
    node->result = outcome;
    node->resume_posted();
}

inline void fire_trigger(const std::shared_ptr<resume_state>& state,
                         int code, resume_outcome outcome) noexcept {
    int expected = 0;
    // CAS, not exchange: the first of {signal, cancel} wins and the value
    // stays resolved; later calls must remain no-ops.
    if (!state->trigger.compare_exchange_strong(expected, code,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
        return;
    }
    std::shared_ptr<resume_node> chain;
    {
        std::lock_guard<std::mutex> lock(state->mu);
        chain = std::move(state->head);
    }
    try {
        std::vector<std::shared_ptr<resume_node>> popped;
        for (std::shared_ptr<resume_node> node = chain; node;
             node = node->next) {
            popped.push_back(node);
        }
        for (const auto& node : popped) deliver(node, outcome);
    } catch (...) {
        // Allocation failure: pending waiters are dropped, never double
        // delivered. chain destruction frees them.
    }
}

// Minimal steady-clock timeout service used by wait_for(). One background
// thread; entries are a multimap of deadline -> entry with a cancellable
// flag, so a cancelled deadline costs one erase-free skip at pop time.
// (Real I/O timers arrive with TASK-099; this service only needs to make
// wait_for real.) Header-only by design: the public resume_signal ABI
// stays linkable without the library, matching the TASK-097 consumer
// sentinel pattern.
struct timer_entry {
    std::mutex mu;
    bool cancelled = false;
    concurrency::unique_function<void()> fn;
};

class timer_queue {
 public:
    using clock = std::chrono::steady_clock;

    class ticket {
     public:
        ticket() = default;

        // Thread-safe, idempotent. The deadline entry becomes a no-op;
        // if it already fired, cancel() does nothing.
        void cancel() {
            if (!entry_) return;
            std::lock_guard<std::mutex> lock(entry_->mu);
            if (!entry_->cancelled) {
                entry_->cancelled = true;
                entry_->fn = nullptr;
            }
        }

        explicit operator bool() const noexcept { return entry_ != nullptr; }

     private:
        friend class timer_queue;
        std::shared_ptr<timer_entry> entry_;
    };

    timer_queue() : thread_([this] { run(); }) { }

    ~timer_queue() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

    ticket schedule(clock::time_point when,
                    concurrency::unique_function<void()> fn) {
        auto entry = std::make_shared<timer_entry>();
        entry->fn = std::move(fn);
        ticket t;
        t.entry_ = entry;
        {
            std::lock_guard<std::mutex> lock(mu_);
            items_.emplace(when, std::move(entry));
        }
        cv_.notify_all();
        return t;
    }

 private:
    void run() {
        std::unique_lock<std::mutex> lock(mu_);
        for (;;) {
            if (stop_) return;
            if (items_.empty()) {
                cv_.wait(lock);
                continue;
            }
            const auto when = items_.begin()->first;
            if (clock::now() < when) {
                cv_.wait_until(lock, when);
                continue;
            }
            std::shared_ptr<timer_entry> entry = std::move(items_.begin()->second);
            items_.erase(items_.begin());
            concurrency::unique_function<void()> fn;
            {
                std::lock_guard<std::mutex> entry_lock(entry->mu);
                if (!entry->cancelled) {
                    fn = std::move(entry->fn);
                    entry->cancelled = true;
                }
            }
            lock.unlock();
            if (fn) fn();  // callbacks post waiter resumptions; never block
            lock.lock();
        }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::multimap<clock::time_point, std::shared_ptr<timer_entry>> items_;
    bool stop_ = false;
    std::thread thread_;
};

inline timer_queue& default_timer_queue() {
    static timer_queue queue;
    return queue;
}

}  // namespace detail

// Awaiter for wait()/wait_for(). Frame-resident; its destructor unlinks
// a still-pending node and cancels the timeout ticket, so destroying the
// waiter (exchange torn down) races a concurrent signal() safely: the
// delivered-CAS keeps delivery exactly once and the frame witness keeps
// any in-flight posted resumption a no-op.
class resume_waiter;

// One-shot, idempotent, thread-safe resume event. Copyable: an exchange
// hands copies to application code while retaining its own handle.
class resume_signal {
 public:
    resume_signal()
        : state_(std::make_shared<detail::resume_state>()) { }

    // The first of {signal, cancel} wins; every later call is a no-op.
    // Never blocks and never resumes a waiter synchronously on the
    // calling thread — waiters wake on their own executor.
    void signal() noexcept {
        detail::fire_trigger(state_, 1, resume_outcome::resumed);
    }

    // Marks the exchange ended (architecture §3.1): waiters observe
    // cancelled instead of resumed.
    void cancel() noexcept {
        detail::fire_trigger(state_, 2, resume_outcome::cancelled);
    }

    // Suspends the awaiting task until the signal fires (resumed) or the
    // exchange cancels (cancelled).
    [[nodiscard]] resume_waiter wait() const;

    // As wait(), but the waiter also observes timeout when the deadline
    // expires before any trigger.
    [[nodiscard]] resume_waiter
    wait_for(std::chrono::steady_clock::duration timeout) const;

 private:
    std::shared_ptr<detail::resume_state> state_;
};

// Awaiter for wait()/wait_for(). Frame-resident; its destructor unlinks
// a still-pending node and cancels the timeout ticket, so destroying the
// waiter (exchange torn down) races a concurrent signal() safely: the
// delivered-CAS keeps delivery exactly once and the frame witness keeps
// any in-flight posted resumption a no-op.
class resume_waiter final {
 public:
    explicit resume_waiter(std::shared_ptr<detail::resume_state> state)
        : state_(std::move(state)),
          node_(std::make_shared<detail::resume_node>()) { }

    resume_waiter(std::shared_ptr<detail::resume_state> state,
                  std::chrono::steady_clock::time_point deadline)
        : state_(std::move(state)),
          node_(std::make_shared<detail::resume_node>()),
          ticket_(std::make_shared<detail::timer_queue::ticket>()),
          deadline_(deadline) { }

    resume_waiter& bind_frame(detail::task_frame_base* frame) noexcept {
        frame_ = frame;
        return *this;
    }

    // Already-fired signal: complete inline on the awaiting frame's own
    // thread (still exactly once — a fresh node has no other claimant).
    bool await_ready() noexcept {
        const int trigger = state_->trigger.load(std::memory_order_acquire);
        if (trigger == 0) return false;
        node_->delivered.store(true, std::memory_order_relaxed);
        node_->result = trigger == 1 ? resume_outcome::resumed
                                     : resume_outcome::cancelled;
        return true;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) {
        // Publishing the node can resume and destroy this awaiter on
        // another worker. Retain everything needed before publication;
        // only these shared/local values are used afterwards.
        auto state = state_;
        auto node = node_;
        auto ticket_slot = ticket_;
        const auto deadline = deadline_;
        node->awaiting = awaiting;
        node->witness = frame_ ? frame_->frame_witness_ptr() : nullptr;
        node->target = frame_ ? frame_->frame_executor() : current_executor();

        int trigger;
        {
            std::lock_guard<std::mutex> lock(state->mu);
            trigger = state->trigger.load(std::memory_order_seq_cst);
            if (trigger == 0) {
                node->next = state->head;
                state->head = node;
            }
        }
        if (trigger != 0) {
            // The trigger raced registration: symmetric transfer stays
            // on the awaiting frame's own thread.
            node->delivered.store(true, std::memory_order_relaxed);
            node->result = trigger == 1 ? resume_outcome::resumed
                                        : resume_outcome::cancelled;
            return awaiting;
        }
        if (ticket_slot) {
            auto ticket = detail::default_timer_queue().schedule(
                deadline, [node] { detail::deliver(node, resume_outcome::timeout); });
            std::lock_guard<std::mutex> lock(state->mu);
            // The waiter may already have died before schedule returned.
            // Serialize installation with destructor cancellation.
            if (node->delivered.load(std::memory_order_acquire)) ticket.cancel();
            else *ticket_slot = std::move(ticket);
        }
        return std::noop_coroutine();
    }

    resume_outcome await_resume() const noexcept { return node_->result; }

    ~resume_waiter() {
        std::lock_guard<std::mutex> lock(state_->mu);
        node_->delivered.store(true, std::memory_order_release);
        if (ticket_) ticket_->cancel();
        std::shared_ptr<detail::resume_node>* link = &state_->head;
        while (*link) {
            if (link->get() == node_.get()) {
                *link = (*link)->next;
                return;
            }
            link = &(*link)->next;
        }
    }

    resume_waiter(const resume_waiter&) = delete;
    resume_waiter& operator=(const resume_waiter&) = delete;
    resume_waiter(resume_waiter&&) = delete;
    resume_waiter& operator=(resume_waiter&&) = delete;

 private:
    std::shared_ptr<detail::resume_state> state_;
    std::shared_ptr<detail::resume_node> node_;
    detail::task_frame_base* frame_ = nullptr;
    std::shared_ptr<detail::timer_queue::ticket> ticket_;
    std::chrono::steady_clock::time_point deadline_{};
};

inline resume_waiter resume_signal::wait() const {
    return resume_waiter(state_);
}

inline resume_waiter
resume_signal::wait_for(std::chrono::steady_clock::duration timeout) const {
    return resume_waiter(state_,
                         std::chrono::steady_clock::now() + timeout);
}

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CONCURRENCY_RESUME_SIGNAL_HPP_
