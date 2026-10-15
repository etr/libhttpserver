/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/concurrency/resume_signal.hpp>
#include <memory>
#include <utility>
#include <vector>

namespace httpserver::detail {

auto resume_node::claim() noexcept -> bool {
    bool expected = false;
    return delivered.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

auto resume_node::resume_posted() noexcept -> void {
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

void deliver(const std::shared_ptr<resume_node>& node, resume_outcome outcome) noexcept {
    if (!node->claim()) return;  // exactly-once: the CAS loser is a no-op
    node->result = outcome;
    node->resume_posted();
}

void fire_trigger(const std::shared_ptr<resume_state>& state, int code, resume_outcome outcome) noexcept {
    int expected = 0;
    // CAS, not exchange: the first of {signal, cancel} wins and the value
    // stays resolved; later calls must remain no-ops.
    if (!state->trigger.compare_exchange_strong(expected, code, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }
    std::shared_ptr<resume_node> chain;
    {
        std::lock_guard<std::mutex> lock(state->mu);
        chain = std::move(state->head);
    }
    try {
        std::vector<std::shared_ptr<resume_node>> popped;
        for (std::shared_ptr<resume_node> node = chain; node; node = node->next) {
            popped.push_back(node);
        }
        for (const auto& node : popped) deliver(node, outcome);
    } catch (...) {
        // Allocation failure: pending waiters are dropped, never double
        // delivered. chain destruction frees them.
    }
}

auto timer_queue::ticket::cancel() -> void {
    if (!entry_) return;
    std::lock_guard<std::mutex> lock(entry_->mu);
    if (!entry_->cancelled) {
        entry_->cancelled = true;
        entry_->fn = nullptr;
    }
}

timer_queue::~timer_queue() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

auto timer_queue::schedule(clock::time_point when, concurrency::unique_function<void()> fn) -> ticket {
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

auto timer_queue::run() -> void {
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

timer_queue& default_timer_queue() {
    static timer_queue queue;
    return queue;
}

}  // namespace httpserver::detail

namespace httpserver {

resume_signal::resume_signal() : state_(std::make_shared<detail::resume_state>()) {}

resume_waiter::resume_waiter(std::shared_ptr<detail::resume_state> state) : state_(std::move(state)), node_(std::make_shared<detail::resume_node>()) {}

resume_waiter::resume_waiter(std::shared_ptr<detail::resume_state> state, std::chrono::steady_clock::time_point deadline)
    : state_(std::move(state)), node_(std::make_shared<detail::resume_node>()), ticket_(std::make_shared<detail::timer_queue::ticket>()), deadline_(deadline) {}

auto resume_waiter::bind_frame(detail::task_frame_base* frame) noexcept -> resume_waiter& {
    frame_ = frame;
    return *this;
}

auto resume_waiter::await_ready() noexcept -> bool {
    const int trigger = state_->trigger.load(std::memory_order_acquire);
    if (trigger == 0) return false;
    node_->delivered.store(true, std::memory_order_relaxed);
    node_->result = trigger == 1 ? resume_outcome::resumed : resume_outcome::cancelled;
    return true;
}

auto resume_waiter::await_suspend(std::coroutine_handle<> awaiting) -> std::coroutine_handle<> {
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
        node->result = trigger == 1 ? resume_outcome::resumed : resume_outcome::cancelled;
        return awaiting;
    }
    if (ticket_slot) {
        auto ticket = detail::default_timer_queue().schedule(deadline, [node] { detail::deliver(node, resume_outcome::timeout); });
        std::lock_guard<std::mutex> lock(state->mu);
        // The waiter may already have died before schedule returned.
        // Serialize installation with destructor cancellation.
        if (node->delivered.load(std::memory_order_acquire))
            ticket.cancel();
        else
            *ticket_slot = std::move(ticket);
    }
    return std::noop_coroutine();
}

resume_waiter::~resume_waiter() {
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

}  // namespace httpserver
