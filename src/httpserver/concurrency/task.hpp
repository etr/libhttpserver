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

#ifndef SRC_HTTPSERVER_CONCURRENCY_TASK_HPP_
#define SRC_HTTPSERVER_CONCURRENCY_TASK_HPP_

// task<T>: the library-defined C++20 coroutine ABI of libhttpserver v3
// (architecture §3.1, DR-V3-003). The contract is public ABI:
//   - task<T> is move-only and is the sole owner of its coroutine frame;
//     copies are a compile error.
//   - Frames start lazily: the body runs only when the task is spawn()ed
//     or co_awaited, never at creation.
//   - A task has a single consumer; consuming twice is a precondition
//     violation reported as a std::logic_error result instead of
//     executing the frame again.
//   - Exceptions thrown by the body are captured into the frame and
//     rethrown at the co_await / delivered through spawn's callback; no
//     exception ever escapes a completion path.
//   - A frame's resumptions always run on the executor captured when the
//     task is started (the spawn executor, or the awaiting task's
//     executor for co_await-started tasks). Completions signaled from
//     other threads are posted to that executor; a coroutine frame is
//     never resumed inline on a foreign thread.
//   - Destroying a task that was never consumed destroys its frame; an
//     un-awaited task is a cancellation at this layer.

#include <atomic>
#include <cstddef>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

// Typed marker thrown by cancellation awaitables (see
// <httpserver/concurrency/cancellation.hpp>) when the stop condition
// fired. A task's promise converts it into the terminal
// http::outcome_code::cancelled result instead of letting the exception
// propagate as an error; consumers that await a cancelled task observe
// the same marker so cancellation propagates.
class cancelled_exception {
 public:
    cancelled_exception() noexcept = default;
};

template<typename T>
class task;

namespace detail {

enum class task_kind : std::uint8_t {
    empty,      // frame not finished yet
    value,      // body completed with a value (or normally, for void)
    exception,  // body threw
    terminal,   // body was cancelled (typed outcome, no exception)
};

enum class task_state : std::uint8_t {
    idle,     // created, never started
    running,  // consumed by exactly one consumer
    done,     // completed; result available for one consumption
};

// Lifetime witness shared between a coroutine frame and every
// "resume this frame" job posted to an executor. A job posted for a
// frame that gets destroyed while the job is in flight observes
// valid == false and does nothing, which closes the destroy-vs-resume
// race exercised by the disconnect/stop suites.
struct frame_witness {
    // Recursive: the completion path (final_awaiter) re-enters the
    // witness while the same thread is inside guarded_resume().
    std::recursive_mutex mu;
    bool valid = true;
};

inline void guarded_resume(const std::shared_ptr<frame_witness>& witness,
                           std::coroutine_handle<> frame) {
    std::lock_guard<std::recursive_mutex> lock(witness->mu);
    if (!witness->valid) return;
    frame.resume();
}

// Heap block backing spawn()'s one-shot completion callback. The
// completion path moves the result out of the frame into this block
// BEFORE posting, so the posted job never touches frame memory (the
// spawned frame self-destroys after completion).
struct spawn_block_base {
    virtual ~spawn_block_base() = default;
    executor* spawn_ex = nullptr;
};

// Shared plumbing of every task coroutine frame. The typed parts (value
// storage, get_return_object) live in task<T>::promise_type; this base
// carries the state machine, the executor affinity, the consumer
// registration, and the spawn delivery. Members are internal to the
// concurrency core (namespace detail, not public ABI) but open to the
// awaiters defined in these headers.
class task_frame_base {
 public:
    task_frame_base() : witness_(std::make_shared<frame_witness>()) { }
    task_frame_base(const task_frame_base&) = delete;
    task_frame_base& operator=(const task_frame_base&) = delete;

    task_state state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    // Transitions idle -> running. False means the frame was already
    // consumed (single-consumer violation or completion already underway).
    bool try_consume() noexcept {
        task_state expected = task_state::idle;
        return state_.compare_exchange_strong(expected, task_state::running,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire);
    }

    executor* frame_executor() const noexcept { return affinity_; }

    std::shared_ptr<frame_witness> frame_witness_ptr() const noexcept {
        return witness_;
    }

    // Lets awaitables that need frame context (task awaiters,
    // cancellation awaitables, resume waiters) receive `this` from the
    // promise's await_transform. Plain awaitables pass through.
    template<typename AW>
    decltype(auto) await_transform(AW&& aw) noexcept {
        if constexpr (requires {
                          std::forward<AW>(aw).bind_frame(
                              static_cast<task_frame_base*>(nullptr));
                      }) {
            return std::forward<AW>(aw).bind_frame(this);
        } else {
            return std::forward<AW>(aw);
        }
    }

    // -- consumer plumbing (used by task_awaiter) ------------------------
    void set_affinity(executor* ex) noexcept { affinity_ = ex; }

    void register_consumer(std::coroutine_handle<> awaiting,
                           executor* consumer_ex,
                           std::shared_ptr<frame_witness> consumer_witness) noexcept {
        consumer_ = awaiting;
        consumer_executor_ = consumer_ex;
        consumer_witness_ = std::move(consumer_witness);
    }

    void set_exception(std::exception_ptr e) noexcept {
        kind_ = task_kind::exception;
        exception_ = std::move(e);
    }

    // Exception entry point from the coroutine machinery: converts a
    // cancelled_exception into a typed terminal outcome and everything
    // else into a captured exception. Never lets an exception escape.
    void capture_exception() noexcept {
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

    // -- spawn plumbing --------------------------------------------------
    void register_spawn_block(std::shared_ptr<spawn_block_base> block,
                              void* promise_erased,
                              void (*pack)(void*, void*),
                              concurrency::unique_function<void()> deliver) noexcept {
        spawn_block_ = std::move(block);
        promise_erased_ = promise_erased;
        pack_result_ = pack;
        deliver_ = std::move(deliver);
    }

    // Completion path, run exactly once from the final suspend awaiter.
    // Delivers the spawn callback (inline when the spawn executor is
    // current, posted otherwise) and returns the handle to resume next:
    // the registered consumer for symmetric transfer, or noop.
    std::coroutine_handle<> finish() noexcept {
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
            const auto witness =
                consumer_witness_ ? consumer_witness_ : witness_;
            std::coroutine_handle<> frame = consumer_;
            try {
                ex->post([witness, frame] { guarded_resume(witness, frame); });
            } catch (...) {
            }
        }
        return std::noop_coroutine();
    }

    // Marks the witness invalid under the lock; used right before a frame
    // is destroyed so that in-flight or later resume jobs become no-ops.
    void invalidate() noexcept {
        std::lock_guard<std::recursive_mutex> lock(witness_->mu);
        witness_->valid = false;
    }

    std::coroutine_handle<> self_handle() const noexcept { return self_; }

    // Internal state; opened to the awaiters in this header set. Not part
    // of the public ABI (namespace detail).
    std::atomic<task_state> state_{task_state::idle};
    task_kind kind_ = task_kind::empty;
    std::exception_ptr exception_;
    http::outcome_code terminal_ = http::outcome_code::ok;

    executor* affinity_ = nullptr;          // executor this frame resumes on
    std::coroutine_handle<> consumer_;       // awaiting coroutine, if any
    executor* consumer_executor_ = nullptr;  // consumer's affinity
    std::shared_ptr<frame_witness> consumer_witness_;

    std::shared_ptr<frame_witness> witness_;
    std::coroutine_handle<> self_;

    std::shared_ptr<spawn_block_base> spawn_block_;
    void* promise_erased_ = nullptr;
    void (*pack_result_)(void*, void*) = nullptr;
    concurrency::unique_function<void()> deliver_;
};

// Final suspend awaiter shared by every task promise. Runs the
// completion path; for spawned (consumer-less) tasks it destroys the
// frame — the self-destroying-coroutine pattern — since no task object
// owns it anymore.
struct final_awaiter {
    bool await_ready() const noexcept { return false; }

    template<typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> frame) noexcept {
        task_frame_base& base = frame.promise();
        const std::coroutine_handle<> next = base.finish();
        if (!base.consumer_) {
            base.invalidate();
            frame.destroy();
            return std::noop_coroutine();
        }
        return next;
    }

    void await_resume() const noexcept { }
};

template<typename T>
class task_awaiter;

template<typename T>
struct spawn_block;

}  // namespace detail

// One-shot delivery envelope handed to spawn()'s callback. Exactly one
// of has_value / is_exception / is_outcome holds.
template<typename T>
class task_result {
 public:
    bool has_value() const noexcept { return kind_ == detail::task_kind::value; }
    bool is_exception() const noexcept {
        return kind_ == detail::task_kind::exception;
    }
    bool has_outcome() const noexcept {
        return kind_ == detail::task_kind::terminal;
    }

    // Precondition: has_value().
    const T& value() const& noexcept { return *value_; }
    T& value() & noexcept { return *value_; }
    T&& value() && noexcept { return std::move(*value_); }

    // Precondition: is_exception().
    const std::exception_ptr& exception() const noexcept { return exception_; }

    // Precondition: has_outcome().
    http::outcome_code outcome() const noexcept { return terminal_; }

 private:
    friend struct detail::spawn_block<T>;

    task_result(detail::task_kind kind,
                std::optional<T> value,
                std::exception_ptr exception,
                http::outcome_code terminal) noexcept
        : kind_(kind),
          value_(std::move(value)),
          exception_(std::move(exception)),
          terminal_(terminal) { }

    detail::task_kind kind_;
    std::optional<T> value_;
    std::exception_ptr exception_;
    http::outcome_code terminal_;
};

// task<void> delivery: a completed void task has no value payload.
template<>
class task_result<void> {
 public:
    bool has_value() const noexcept { return kind_ == detail::task_kind::value; }
    bool is_exception() const noexcept {
        return kind_ == detail::task_kind::exception;
    }
    bool has_outcome() const noexcept {
        return kind_ == detail::task_kind::terminal;
    }

    const std::exception_ptr& exception() const noexcept { return exception_; }

    http::outcome_code outcome() const noexcept { return terminal_; }

 private:
    friend struct detail::spawn_block<void>;

    task_result(detail::task_kind kind,
                std::exception_ptr exception,
                http::outcome_code terminal) noexcept
        : kind_(kind),
          exception_(std::move(exception)),
          terminal_(terminal) { }

    detail::task_kind kind_;
    std::exception_ptr exception_;
    http::outcome_code terminal_;
};

namespace detail {

template<typename T>
struct spawn_block final : spawn_block_base {
    concurrency::unique_function<void(task_result<T>)> callback;
    detail::task_kind kind = detail::task_kind::empty;
    std::optional<T> value;
    std::exception_ptr exception;
    http::outcome_code terminal = http::outcome_code::ok;

    void run() {
        callback(task_result<T>(kind, std::move(value),
                                std::move(exception), terminal));
    }
};

// Value-less completion envelope for task<void>.
template<>
struct spawn_block<void> final : spawn_block_base {
    concurrency::unique_function<void(task_result<void>)> callback;
    detail::task_kind kind = detail::task_kind::empty;
    std::exception_ptr exception;
    http::outcome_code terminal = http::outcome_code::ok;

    void run() {
        callback(task_result<void>(kind, std::move(exception), terminal));
    }
};

template<typename T>
class task_awaiter final {
 public:
    using handle_type = std::coroutine_handle<
        typename task<T>::promise_type>;

    explicit task_awaiter(handle_type frame) noexcept : frame_(frame) { }
    task_awaiter(task_awaiter&&) noexcept = default;

    task_awaiter& bind_frame(task_frame_base* consumer) noexcept {
        consumer_ = consumer;
        return *this;
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        if (!frame_) {
            // Moved-from / invalid task: precondition violation, delivered
            // as an exception result instead of executing a dead frame.
            fail_consumer("httpserver::task awaited while invalid");
            return awaiting;
        }
        auto& promise = frame_.promise();
        executor* const consumer_ex =
            consumer_ ? consumer_->frame_executor() : current_executor();
        switch (promise.state()) {
            case task_state::done:
                // Already completed (spawn()ed elsewhere): consume the
                // result inline on the consumer's own thread/executor.
                return awaiting;
            case task_state::idle:
                if (!promise.try_consume()) {
                    fail_consumer("httpserver::task consumed twice");
                    return awaiting;
                }
                promise.set_affinity(consumer_ex);
                promise.register_consumer(
                    awaiting, consumer_ex,
                    consumer_ ? consumer_->frame_witness_ptr() : nullptr);
                return frame_;  // symmetric transfer into the task body
            case task_state::running:
            default:
                fail_consumer("httpserver::task consumed twice");
                return awaiting;
        }
    }

    T await_resume() {
        if (!frame_) {
            throw std::logic_error(
                "httpserver::task awaited while invalid");
        }
        auto& promise = frame_.promise();
        switch (promise.kind_) {
            case task_kind::value:
                if constexpr (!std::is_void_v<T>) {
                    return std::move(*promise.value_);
                } else {
                    return;
                }
            case task_kind::exception:
                std::rethrow_exception(promise.exception_);
            case task_kind::terminal:
                // Propagate cancellation into the consumer; the consumer's
                // promise converts it into its own terminal outcome.
                throw cancelled_exception();
            case task_kind::empty:
            default:
                throw std::logic_error(
                    "httpserver::task resumed before completion");
        }
    }

 private:
    void fail_consumer(const char* message) noexcept {
        if (consumer_ != nullptr) {
            consumer_->set_exception(
                std::make_exception_ptr(std::logic_error(message)));
        }
    }

    handle_type frame_;
    task_frame_base* consumer_ = nullptr;
};

}  // namespace detail

template<typename T>
class [[nodiscard]] task {
 public:
    struct promise_type final : detail::task_frame_base {
        promise_type() {
            self_ = std::coroutine_handle<promise_type>::from_promise(*this);
            promise_erased_ = this;
        }

        task get_return_object() {
            return task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        detail::final_awaiter final_suspend() noexcept { return {}; }

        template<typename V>
        void return_value(V&& value) {
            value_.emplace(std::forward<V>(value));
            kind_ = detail::task_kind::value;
        }

        void unhandled_exception() noexcept { capture_exception(); }

        static void pack_result(void* erased_promise, void* erased_block) {
            auto& promise = *static_cast<promise_type*>(erased_promise);
            auto& block = *static_cast<detail::spawn_block<T>*>(erased_block);
            block.kind = promise.kind_;
            if (promise.kind_ == detail::task_kind::value) {
                block.value = std::move(promise.value_);
            } else if (promise.kind_ == detail::task_kind::exception) {
                block.exception = promise.exception_;
            } else if (promise.kind_ == detail::task_kind::terminal) {
                block.terminal = promise.terminal_;
            }
        }

        std::optional<T> value_;
    };

    task() noexcept = default;

    task(task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) { }

    task& operator=(task&& other) noexcept {
        if (this != &other) {
            destroy_frame();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    ~task() { destroy_frame(); }

    bool valid() const noexcept { return handle_ != nullptr; }

    // Starts the frame when awaited: the task runs on the awaiting task's
    // executor and its completion resumes the awaiter there.
    auto operator co_await() && noexcept {
        return detail::task_awaiter<T>(handle_);
    }

    // Relinquishes frame ownership (used by spawn()).
    std::coroutine_handle<promise_type> release() noexcept {
        return std::exchange(handle_, {});
    }

 private:
    friend struct promise_type;

    explicit task(std::coroutine_handle<promise_type> frame) noexcept
        : handle_(frame) { }

    void destroy_frame() noexcept {
        if (!handle_) return;
        handle_.promise().invalidate();
        handle_.destroy();
        handle_ = {};
    }

    std::coroutine_handle<promise_type> handle_;
};

// task<void> specialization: return_void instead of return_value and a
// value-less spawn envelope.
template<>
class [[nodiscard]] task<void> {
 public:
    struct promise_type final : detail::task_frame_base {
        promise_type() {
            self_ = std::coroutine_handle<promise_type>::from_promise(*this);
            promise_erased_ = this;
        }

        task get_return_object() {
            return task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        detail::final_awaiter final_suspend() noexcept { return {}; }

        void return_void() noexcept {
            kind_ = detail::task_kind::value;
        }

        void unhandled_exception() noexcept { capture_exception(); }

        static void pack_result(void* erased_promise, void* erased_block) {
            auto& promise = *static_cast<promise_type*>(erased_promise);
            auto& block = *static_cast<detail::spawn_block<void>*>(erased_block);
            block.kind = promise.kind_;
            if (promise.kind_ == detail::task_kind::exception) {
                block.exception = promise.exception_;
            } else if (promise.kind_ == detail::task_kind::terminal) {
                block.terminal = promise.terminal_;
            }
        }
    };

    task() noexcept = default;

    task(task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) { }

    task& operator=(task&& other) noexcept {
        if (this != &other) {
            destroy_frame();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    ~task() { destroy_frame(); }

    bool valid() const noexcept { return handle_ != nullptr; }

    auto operator co_await() && noexcept {
        return detail::task_awaiter<void>(handle_);
    }

    std::coroutine_handle<promise_type> release() noexcept {
        return std::exchange(handle_, {});
    }

 private:
    friend struct promise_type;

    explicit task(std::coroutine_handle<promise_type> frame) noexcept
        : handle_(frame) { }

    void destroy_frame() noexcept {
        if (!handle_) return;
        handle_.promise().invalidate();
        handle_.destroy();
        handle_ = {};
    }

    std::coroutine_handle<promise_type> handle_;
};

namespace detail {

// Common tail of spawn(): consume the frame, install the typed delivery
// block, and hand the start job to the executor. The frame starts (and,
// for tasks with no consumer, self-destroys after completing) when the
// executor runs the job.
template<typename T>
void spawn_impl(executor& ex,
                task<T> t,
                concurrency::unique_function<void(task_result<T>)> on_done) {
    auto frame = t.release();
    if (!frame) {
        // Invalid task: deliver the violation through the callback.
        auto block = std::make_shared<spawn_block<T>>();
        block->spawn_ex = &ex;
        block->kind = task_kind::exception;
        block->exception = std::make_exception_ptr(
            std::logic_error("httpserver::spawn called with an invalid task"));
        block->callback = std::move(on_done);
        ex.post([block] { block->run(); });
        return;
    }
    auto& promise = frame.promise();
    auto block = std::make_shared<spawn_block<T>>();
    block->spawn_ex = &ex;
    block->callback = std::move(on_done);
    if (!promise.try_consume()) {
        block->kind = task_kind::exception;
        block->exception = std::make_exception_ptr(
            std::logic_error("httpserver::task consumed twice"));
        ex.post([block] { block->run(); });
        return;
    }
    promise.set_affinity(&ex);
    promise.register_spawn_block(
        block, &promise, &task<T>::promise_type::pack_result,
        [block] { block->run(); });
    const auto witness = promise.frame_witness_ptr();
    ex.post([witness, frame] { guarded_resume(witness, frame); });
}

}  // namespace detail

// Starts `t` on `ex` and delivers its completion exactly once through
// `on_done`, on `ex`. The callback receives a task_result<T> carrying the
// value, the body's exception, or a terminal outcome (e.g. cancelled).
template<typename T>
void spawn(executor& ex,
           task<T> t,
           concurrency::unique_function<void(
               task_result<std::type_identity_t<T>>)> on_done) {
    detail::spawn_impl(ex, std::move(t), std::move(on_done));
}

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CONCURRENCY_TASK_HPP_
