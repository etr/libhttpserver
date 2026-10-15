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

// Out-of-line machinery of the private operation I/O contract
// (TASK-099): the op_state terminal claim, the mutex-guarded waiter
// slot, and the owner-side application path. The resumption discipline
// reuses the proven frame_witness/guarded_resume pair of the TASK-098
// concurrency core instead of inventing new synchronization.

#include "httpserver/detail/io_operation.hpp"

#include <exception>
#include <memory>
#include <utility>

namespace httpserver {
namespace detail {

namespace {

// Resumes a frame on its own executor: posted when the executor is not
// current (never inline on a foreign thread), witness-guarded so a
// frame destroyed while the job is in flight observes valid == false
// and does nothing. Same contract as resume_node::resume_posted.
void resume_guarded(executor* resume_ex,
                    const std::shared_ptr<frame_witness>& witness,
                    std::coroutine_handle<> frame) noexcept {
    if (resume_ex != nullptr && !resume_ex->is_current()) {
        try {
            resume_ex->post([witness, frame] {
                guarded_resume(witness, frame);
            });
        } catch (...) {
            // post() is documented not to throw; containment keeps a
            // throwing executor from escaping a completion path.
        }
    } else if (witness) {
        guarded_resume(witness, frame);
    } else {
        frame.resume();
    }
}

}  // namespace

op_state::op_state(io_op_kind kind, io_connection_owner* owner,
                   std::uint64_t connection, op_payload payload)
    : kind_(kind),
      owner_(owner),
      connection_(connection),
      payload_(std::move(payload)) {
}

bool op_state::arm_waiter(std::coroutine_handle<> frame,
                          const std::shared_ptr<frame_witness>& witness,
                          executor* resume_ex) {
    std::lock_guard<std::mutex> lock(mu_);
    if (applied_.load(std::memory_order_acquire)) {
        return false;  // raced the application: complete via the caller
    }
    frame_ = frame;
    witness_ = witness;
    resume_ex_ = resume_ex;
    return true;
}

void op_state::disarm_waiter() noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    frame_ = {};
    witness_ = nullptr;
    resume_ex_ = nullptr;
}

void op_state::apply(io_result result) {
    std::coroutine_handle<> frame;
    std::shared_ptr<frame_witness> witness;
    executor* resume_ex = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        result_ = result;
        // The applied flag is part of the same critical section as the
        // slot consumption, exactly like resume_waiter's trigger-before-
        // unlink ordering: arm_waiter checks the flag under this mutex,
        // so it either runs first (its frame is consumed below and
        // resumed) or observes applied == true and completes via
        // symmetric transfer. Storing the flag after the unlock would
        // let a suspending awaiter install into a slot this apply had
        // already consumed and never be resumed -- a lost completion.
        applied_.store(true, std::memory_order_release);
        frame = std::exchange(frame_, {});
        witness = std::move(witness_);
        resume_ex = std::exchange(resume_ex_, nullptr);
    }
    if (frame) {
        resume_guarded(resume_ex, witness, frame);
    }
}

std::coroutine_handle<> op_awaiter::await_suspend(
    std::coroutine_handle<> awaiting) {
    if (!state_->arm_waiter(awaiting, witness_, resume_ex_)) {
        // The completion raced the registration and already applied:
        // resume via symmetric transfer, still on this frame's thread.
        return awaiting;
    }
    return std::noop_coroutine();
}

void op_handle::arm_stop(std::stop_token token) {
    if (state_ == nullptr) {
        throw std::logic_error(
            "httpserver::io operation is moved-from");
    }
    if (backend_ == nullptr) {
        throw std::logic_error(
            "httpserver::io operation arm_stop before submit");
    }
    if (state_->is_terminal()) {
        return;  // arming after a terminal result is a no-op
    }
    struct stop_canceler {
        io_backend* backend;
        std::shared_ptr<op_state> state;

        void operator()() const noexcept {
            try {
                backend->request_cancel(*state);
            } catch (...) {
                // Completion paths never let exceptions escape.
            }
        }
    };
    // Heap-static callback held by a movable shared_ptr: std::stop_callback
    // itself is neither copyable nor movable, so a direct member would
    // break the handle's move semantics.
    std::shared_ptr<void> registration =
        std::make_shared<std::stop_callback<stop_canceler>>(
            std::move(token), stop_canceler{backend_, state_});
    if (state_->is_terminal()) {
        // The op completed between the check and the registration (the
        // callback may have run inline on an already-requested token);
        // the claim CAS decided, so unregister and stay a no-op.
        return;
    }
    stop_registration_ = std::move(registration);
}

}  // namespace detail
}  // namespace httpserver

/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */

namespace httpserver::detail {

auto op_state::claim_terminal() noexcept -> bool {
    bool expected = false;
    return terminal_.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
}

auto op_state::stored_result() const -> io_result {
    std::lock_guard<std::mutex> lock(mu_);
    return result_;
}

auto op_state::try_flag(std::atomic<bool>& flag) noexcept -> bool {
    bool expected = false;
    return flag.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire);
}

op_awaiter::op_awaiter(std::shared_ptr<op_state> state,
               task_frame_base* frame) noexcept
        : state_(std::move(state)) {
    if (frame != nullptr) bind_frame(frame);
}

auto op_awaiter::bind_frame(task_frame_base* frame) noexcept -> op_awaiter& {
    if (frame != nullptr) {
        witness_ = frame->frame_witness_ptr();
        resume_ex_ = frame->frame_executor();
    }
    return *this;
}

op_awaiter::~op_awaiter() {
    if (state_) state_->disarm_waiter();
}

auto op_handle::bind_frame(task_frame_base* frame) noexcept -> op_handle& {
    frame_ = frame;
    return *this;
}

auto op_handle::submit(io_backend& backend) -> void {
    if (state_ == nullptr || !state_->mark_submitted()) {
        throw std::logic_error(
            "httpserver::io operation submitted twice");
    }
    backend_ = &backend;
    backend.submit(*state_);
}

auto op_handle::operator co_await() -> op_awaiter {
    if (state_ == nullptr || !state_->mark_awaited()) {
        throw std::logic_error(
            "httpserver::io operation awaited twice");
    }
    return frame_ != nullptr ? op_awaiter(state_, frame_)
                             : op_awaiter(state_);
}

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
accept_operation::accept_operation(io_connection_owner& owner, std::uint64_t connection)
    : op_handle(std::make_shared<op_state>(io_op_kind::accept, &owner, connection, op_payload{accept_payload{}})) {}
// NOLINTEND

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
read_operation::read_operation(io_connection_owner& owner, std::uint64_t connection, std::span<std::byte> buffer, std::chrono::steady_clock::time_point deadline)
    : op_handle(std::make_shared<op_state>(io_op_kind::read, &owner, connection, op_payload{read_payload{buffer, deadline}})) {}
// NOLINTEND

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
write_operation::write_operation(io_connection_owner& owner, std::uint64_t connection, std::span<const std::byte> bytes, std::chrono::steady_clock::time_point deadline)
    : op_handle(std::make_shared<op_state>(io_op_kind::write, &owner, connection, op_payload{write_payload{bytes, deadline}})) {}
// NOLINTEND

udp_receive_operation::udp_receive_operation(io_connection_owner& owner, std::uint64_t socket, std::size_t capacity)
    : op_handle(std::make_shared<op_state>(io_op_kind::udp_receive, &owner, socket, make_payload(capacity))) {}

auto udp_receive_operation::make_payload(std::size_t capacity) -> op_payload {
    auto packet = std::make_shared<io_datagram>();
    const bool valid = capacity <= k_max_datagram_bytes;
    if (valid) packet->bytes.resize(capacity);
    return udp_payload{std::move(packet), valid, valid ? capacity : 0};
}

udp_send_operation::udp_send_operation(io_connection_owner& owner, std::uint64_t socket, std::span<const std::byte> bytes, datagram_endpoint peer)
    : op_handle(std::make_shared<op_state>(io_op_kind::udp_send, &owner, socket, make_payload(bytes, peer))) {}

auto udp_send_operation::make_payload(std::span<const std::byte> bytes, datagram_endpoint peer) -> op_payload {
    auto packet = std::make_shared<io_datagram>();
    const bool valid = bytes.size() <= k_max_datagram_bytes;
    if (valid) packet->bytes.assign(bytes.begin(), bytes.end());
    packet->peer = peer;
    return udp_payload{std::move(packet), valid, valid ? bytes.size() : 0};
}

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
timer_operation::timer_operation(io_connection_owner& owner, std::uint64_t connection, std::chrono::steady_clock::time_point deadline)
    : op_handle(std::make_shared<op_state>(io_op_kind::timer, &owner, connection, op_payload{timer_payload{deadline}})) {}
// NOLINTEND

wake_operation::wake_operation(io_connection_owner& owner, std::uint64_t connection) : op_handle(std::make_shared<op_state>(io_op_kind::wake, &owner, connection, op_payload{wake_payload{}})) {}

// cpplint cannot track this constructor's nested qualifiers or braced initializers.
// NOLINTBEGIN(whitespace/indent_namespace)
cancel_operation::cancel_operation(io_connection_owner& owner, std::uint64_t connection, const op_handle& target)
    : op_handle(std::make_shared<op_state>(io_op_kind::cancel, &owner, connection, op_payload{cancel_payload{target.state_}})) {}
// NOLINTEND

}  // namespace httpserver::detail
