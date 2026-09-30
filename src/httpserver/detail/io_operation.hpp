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

// Private operation I/O contract of the v3 native engine (architecture
// §3.4, DR-V3-004, TASK-099). Private io_operation objects describe
// accept, receive (read), send (write), timer, wake, and cancellation
// completions with storage alive through completion. One connection has
// one io_connection_owner; a backend completion wins exactly one
// terminal claim on the op state and hands the record to that owner,
// which serializes application on the connection's executor -- backend
// delivery order never defines protocol order.
//
// Lifetime contract: every object here travels by shared_ptr. Backends
// and owners never touch raw op addresses, so destroying an operation
// handle after submission is memory-safe: a pending awaiter is
// uninstalled by the awaiter destructor, and a posted resumption is
// witness-guarded. The owner and the io_backend passed to submit() must
// outlive every handle that references them.
//
// Vocabulary is http::outcome_code only (REQ-037); misuse (double
// submit, double await, stop armed without a backend) is reported as
// std::logic_error or a typed invalid_state outcome, never as an OS
// error. No OS-socket vocabulary appears in this layer; real socket
// backends arrive with TASK-100.
#if !defined(HTTPSERVER_COMPILATION)
#error "io_operation.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_IO_OPERATION_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_OPERATION_HPP_

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <utility>
#include <variant>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {
namespace detail {

class io_connection_owner;
class op_state;
class op_handle;
class op_awaiter;

// The six operation kinds of the private completion model (§3.4).
enum class io_op_kind : std::uint8_t {
    accept,
    read,
    write,
    timer,
    wake,
    cancel,
};

// One terminal result. The vocabulary is http::outcome_code only.
struct io_result {
    http::outcome_code code = http::outcome_code::ok;
    std::size_t transferred = 0;   // read/write: bytes; other kinds: 0
    std::uint64_t accepted_id = 0;  // accept only: fabricated connection id
};

// Per-kind owned payload carried inside op_state. Buffers are
// caller-owned memory the operation borrows until the terminal result.
struct accept_payload { };
struct read_payload {
    std::span<std::byte> buffer;
};
struct write_payload {
    std::span<const std::byte> bytes;
};
struct timer_payload {
    std::chrono::steady_clock::time_point deadline;
};
struct wake_payload { };
struct cancel_payload {
    std::shared_ptr<op_state> target;
};

using op_payload = std::variant<accept_payload, read_payload, write_payload,
                                timer_payload, wake_payload, cancel_payload>;

// Shared, lifetime-safe terminal state of one operation. Exactly-once
// termination is enforced by claim_terminal(): duplicate completions,
// late cancels, and close-after-complete all lose the CAS and become
// no-ops. This is the single enforcement point of the whole layer.
class op_state final : public std::enable_shared_from_this<op_state> {
 public:
    op_state(io_op_kind kind, io_connection_owner* owner,
             std::uint64_t connection, op_payload payload);

    op_state(const op_state&) = delete;
    op_state& operator=(const op_state&) = delete;

    io_op_kind kind() const noexcept { return kind_; }
    io_connection_owner* owner() const noexcept { return owner_; }
    std::uint64_t connection() const noexcept { return connection_; }
    const op_payload& payload() const noexcept { return payload_; }

    // Submission order as metadata, bound by the backend at submit.
    // Monotonic per backend; never defines delivery order.
    std::uint64_t sequence() const noexcept {
        return sequence_.load(std::memory_order_acquire);
    }
    void set_sequence(std::uint64_t sequence) noexcept {
        sequence_.store(sequence, std::memory_order_release);
    }

    // One-shot submission/await/consumption flags (CAS-backed so they
    // survive handle moves). False means the flag was already taken.
    bool mark_submitted() noexcept {
        return try_flag(submitted_);
    }
    bool mark_awaited() noexcept {
        return try_flag(awaited_);
    }

    // The single enforcement point: first terminal claimant wins.
    bool claim_terminal() noexcept {
        bool expected = false;
        return terminal_.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
    }
    bool is_terminal() const noexcept {
        return terminal_.load(std::memory_order_acquire);
    }

    // Waiter plumbing (used by op_awaiter). arm_waiter installs the
    // suspended frame under the state mutex; false means a completion
    // already applied (the caller completes via symmetric transfer).
    bool arm_waiter(std::coroutine_handle<> frame,
                    const std::shared_ptr<frame_witness>& witness,
                    executor* resume_ex);
    void disarm_waiter() noexcept;

    // Owner path: stores the decided result, marks applied, and resumes
    // the armed waiter (posted to its executor unless already current;
    // witness-guarded so a destroyed frame turns the job into a no-op).
    void apply(io_result result);

    bool applied() const noexcept {
        return applied_.load(std::memory_order_acquire);
    }

    // Precondition: applied().
    io_result stored_result() const {
        std::lock_guard<std::mutex> lock(mu_);
        return result_;
    }

 private:
    static bool try_flag(std::atomic<bool>& flag) noexcept {
        bool expected = false;
        return flag.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
    }

    const io_op_kind kind_;
    io_connection_owner* const owner_;
    const std::uint64_t connection_;
    op_payload payload_;

    std::atomic<bool> submitted_{false};
    std::atomic<bool> awaited_{false};
    std::atomic<bool> terminal_{false};
    std::atomic<bool> applied_{false};
    std::atomic<std::uint64_t> sequence_{0};

    mutable std::mutex mu_;              // guards the waiter slot below
    io_result result_{};
    std::coroutine_handle<> frame_{};
    std::shared_ptr<frame_witness> witness_;
    executor* resume_ex_ = nullptr;
};

// Abstract backend seam (TASK-100's poll/WSAPoll drivers implement it;
// TASK-124/126's readiness adapter subclasses it without rework).
// Deliberately minimal: readiness/interest APIs are later-task surface.
class io_backend {
 public:
    virtual ~io_backend() = default;

    // Registers a freshly submitted operation. The backend binds the
    // op's sequence and takes shared ownership of the state.
    virtual void submit(op_state& op) = 0;

    // Attempts to cancel @p target. Returns the outcome the canceling
    // side reports: ok when the target was delivered cancelled,
    // invalid_state when it had already reached a terminal result some
    // other way, connection_closed when the backend is closed.
    virtual http::outcome_code request_cancel(op_state& target) = 0;

 protected:
    io_backend() = default;

 private:
    io_backend(const io_backend&) = delete;
    io_backend& operator=(const io_backend&) = delete;
};

// Awaiter produced by op_handle::operator co_await. Frame-resident: its
// destructor disarms the waiter, so destroying the awaiting task after
// submission races a concurrent completion safely (the applied flag
// keeps delivery exactly once and the frame witness keeps any in-flight
// posted resumption a no-op).
class op_awaiter final {
 public:
    explicit op_awaiter(std::shared_ptr<op_state> state) noexcept
        : state_(std::move(state)) { }

    op_awaiter(const op_awaiter&) = delete;
    op_awaiter& operator=(const op_awaiter&) = delete;
    op_awaiter(op_awaiter&&) = delete;
    op_awaiter& operator=(op_awaiter&&) = delete;

    op_awaiter& bind_frame(task_frame_base* frame) noexcept {
        if (frame != nullptr) {
            witness_ = frame->frame_witness_ptr();
            resume_ex_ = frame->frame_executor();
        }
        return *this;
    }

    // A terminal-and-applied operation completes inline on the awaiting
    // frame's own thread. A claimed-but-not-yet-applied operation
    // suspends, so per-connection effect order == resumption order.
    bool await_ready() const noexcept {
        return state_->applied();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting);

    // Precondition: applied().
    io_result await_resume() const {
        return state_->stored_result();
    }

    ~op_awaiter() {
        if (state_) state_->disarm_waiter();
    }

 private:
    std::shared_ptr<op_state> state_;
    std::shared_ptr<frame_witness> witness_;
    executor* resume_ex_ = nullptr;
};

// Common machinery of the six typed operation handles: non-copyable,
// movable, one owner (connection) fixed at construction, payload owned
// until the terminal result. submit() is one-shot; operator co_await is
// single-consumer; both violations are std::logic_error.
class op_handle {
 public:
    // One-shot: binds the backend and hands the op state over. A second
    // submit is a std::logic_error (task's single-consumer discipline).
    void submit(io_backend& backend) {
        if (state_ == nullptr || !state_->mark_submitted()) {
            throw std::logic_error(
                "httpserver::io operation submitted twice");
        }
        backend_ = &backend;
        backend.submit(*state_);
    }

    // Arms cancellation: registers a std::stop_callback that calls
    // backend.request_cancel(*state). The terminal claim CAS resolves
    // the race with a concurrent backend completion; std::stop_callback's
    // blocking destructor gives the same destroy-during-callback safety
    // cancellation.hpp relies on. Arming after the op reached a terminal
    // result is a no-op. Requires a prior submit().
    void arm_stop(std::stop_token token);

    // Single-consumer await; a second await is a std::logic_error.
    op_awaiter operator co_await() && {
        if (state_ == nullptr || !state_->mark_awaited()) {
            throw std::logic_error(
                "httpserver::io operation awaited twice");
        }
        return op_awaiter(state_);
    }

    io_op_kind kind() const noexcept { return state_->kind(); }
    std::uint64_t connection() const noexcept { return state_->connection(); }
    std::uint64_t sequence() const noexcept { return state_->sequence(); }
    bool is_terminal() const noexcept { return state_->is_terminal(); }

    // White-box seam for the detail unit tests (and the backend
    // completion path of tests that drive op_state directly).
    const std::shared_ptr<op_state>& state() const noexcept {
        return state_;
    }

 protected:
    explicit op_handle(std::shared_ptr<op_state> state) noexcept
        : state_(std::move(state)) { }
    op_handle(op_handle&&) noexcept = default;
    op_handle& operator=(op_handle&&) noexcept = default;
    ~op_handle() = default;

 private:
    op_handle(const op_handle&) = delete;
    op_handle& operator=(const op_handle&) = delete;
    friend class cancel_operation;

    std::shared_ptr<op_state> state_;
    io_backend* backend_ = nullptr;
    // Type-erased holder for the heap-allocated std::stop_callback
    // (stop_callback itself is neither copyable nor movable).
    std::shared_ptr<void> stop_registration_;
};

// The six typed, owned operations (DR-V3-004).
class accept_operation final : public op_handle {
 public:
    accept_operation(io_connection_owner& owner, std::uint64_t connection)
        : op_handle(std::make_shared<op_state>(
              io_op_kind::accept, &owner, connection,
              op_payload{accept_payload{}})) { }
};

class read_operation final : public op_handle {
 public:
    read_operation(io_connection_owner& owner, std::uint64_t connection,
                   std::span<std::byte> buffer)
        : op_handle(std::make_shared<op_state>(
              io_op_kind::read, &owner, connection,
              op_payload{read_payload{buffer}})) { }

    std::span<std::byte> buffer() const noexcept {
        return std::get<read_payload>(state()->payload()).buffer;
    }
};

class write_operation final : public op_handle {
 public:
    write_operation(io_connection_owner& owner, std::uint64_t connection,
                    std::span<const std::byte> bytes)
        : op_handle(std::make_shared<op_state>(
              io_op_kind::write, &owner, connection,
              op_payload{write_payload{bytes}})) { }

    std::span<const std::byte> bytes() const noexcept {
        return std::get<write_payload>(state()->payload()).bytes;
    }
};

class timer_operation final : public op_handle {
 public:
    timer_operation(io_connection_owner& owner, std::uint64_t connection,
                    std::chrono::steady_clock::time_point deadline)
        : op_handle(std::make_shared<op_state>(
              io_op_kind::timer, &owner, connection,
              op_payload{timer_payload{deadline}})) { }

    std::chrono::steady_clock::time_point deadline() const noexcept {
        return std::get<timer_payload>(state()->payload()).deadline;
    }
};

class wake_operation final : public op_handle {
 public:
    wake_operation(io_connection_owner& owner, std::uint64_t connection)
        : op_handle(std::make_shared<op_state>(
              io_op_kind::wake, &owner, connection,
              op_payload{wake_payload{}})) { }
};

class cancel_operation final : public op_handle {
 public:
    cancel_operation(io_connection_owner& owner, std::uint64_t connection,
                     const op_handle& target)
        : op_handle(std::make_shared<op_state>(
              io_op_kind::cancel, &owner, connection,
              op_payload{cancel_payload{target.state_}})) { }

    std::shared_ptr<op_state> target() const noexcept {
        return std::get<cancel_payload>(state()->payload()).target;
    }
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_IO_OPERATION_HPP_
