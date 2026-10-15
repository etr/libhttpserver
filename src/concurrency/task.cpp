/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/concurrency/task.hpp>
#include <memory>
#include <utility>

namespace httpserver::detail {

std::shared_ptr<void>& current_task_executor_lifetime() noexcept {
    thread_local std::shared_ptr<void> lifetime;
    return lifetime;
}

void guarded_resume(const std::shared_ptr<frame_witness>& witness, std::coroutine_handle<> frame) {
    std::lock_guard<std::recursive_mutex> lock(witness->mu);
    if (!witness->valid) return;
    frame.resume();
}

task_frame_base::task_frame_base() : witness_(std::make_shared<frame_witness>()) {}

auto task_frame_base::try_consume() noexcept -> bool {
    task_state expected = task_state::idle;
    return state_.compare_exchange_strong(expected, task_state::running, std::memory_order_acq_rel, std::memory_order_acquire);
}

auto task_frame_base::set_affinity(executor* ex, std::shared_ptr<void> lifetime) noexcept -> void {
    affinity_ = ex;
    witness_->executor_lifetime = std::move(lifetime);
}

auto task_frame_base::register_consumer(std::coroutine_handle<> awaiting, executor* consumer_ex, std::shared_ptr<frame_witness> consumer_witness) noexcept -> void {
    consumer_ = awaiting;
    consumer_executor_ = consumer_ex;
    consumer_witness_ = std::move(consumer_witness);
}

auto task_frame_base::set_exception(std::exception_ptr e) noexcept -> void {
    kind_ = task_kind::exception;
    exception_ = std::move(e);
}

auto task_frame_base::capture_exception() noexcept -> void {
    try {
        throw;
    } catch (const cancelled_exception&) {
        kind_ = task_kind::terminal;
        terminal_ = http::outcome_code::cancelled;
    } catch (...) {
        kind_ = task_kind::exception;
        exception_ = std::current_exception();
    }
}

auto task_frame_base::register_spawn_block(std::shared_ptr<spawn_block_base> block, void* promise_erased, void (*pack)(void*, void*), concurrency::unique_function<void()> deliver) noexcept -> void {
    spawn_block_ = std::move(block);
    promise_erased_ = promise_erased;
    pack_result_ = pack;
    deliver_ = std::move(deliver);
}

auto task_frame_base::finish() noexcept -> std::coroutine_handle<> {
    state_.store(task_state::done, std::memory_order_release);
    if (spawn_block_) {
        pack_result_(promise_erased_, spawn_block_.get());
        auto deliver = std::move(deliver_);
        deliver_ = concurrency::unique_function<void()>();
        executor* const ex = spawn_block_->spawn_ex;
        // post() is documented not to throw; a throw here would lose
        // the delivery, so it is contained.
        try {
            if (ex != nullptr && !ex->is_current()) {
                ex->post(std::move(deliver));
            } else {
                deliver();
            }
        } catch (...) {
        }
    }
    if (consumer_) {
        executor* const ex = consumer_executor_;
        if (ex == nullptr || ex->is_current()) {
            return consumer_;
        }
        const auto witness = consumer_witness_ ? consumer_witness_ : witness_;
        std::coroutine_handle<> frame = consumer_;
        try {
            ex->post([witness, frame] { guarded_resume(witness, frame); });
        } catch (...) {
        }
    }
    return std::noop_coroutine();
}

auto task_frame_base::invalidate() noexcept -> void {
    std::lock_guard<std::recursive_mutex> lock(witness_->mu);
    witness_->valid = false;
}

}  // namespace httpserver::detail

namespace httpserver {

task_result<void>::task_result(detail::task_kind kind, std::exception_ptr exception, http::outcome_code terminal) noexcept : kind_(kind), exception_(std::move(exception)), terminal_(terminal) {}

task<void>::promise_type::promise_type() {
    self_ = std::coroutine_handle<promise_type>::from_promise(*this);
    promise_erased_ = this;
}

auto task<void>::promise_type::pack_result(void* erased_promise, void* erased_block) -> void {
    auto& promise = *static_cast<promise_type*>(erased_promise);
    auto& block = *static_cast<detail::spawn_block<void>*>(erased_block);
    block.kind = promise.kind_;
    if (promise.kind_ == detail::task_kind::exception) {
        block.exception = promise.exception_;
    } else if (promise.kind_ == detail::task_kind::terminal) {
        block.terminal = promise.terminal_;
    }
}

auto task<void>::operator=(task&& other) noexcept -> task& {
    if (this != &other) {
        destroy_frame();
        handle_ = std::exchange(other.handle_, {});
    }
    return *this;
}

auto task<void>::destroy_frame() noexcept -> void {
    if (!handle_) return;
    handle_.promise().invalidate();
    handle_.destroy();
    handle_ = {};
}

}  // namespace httpserver
