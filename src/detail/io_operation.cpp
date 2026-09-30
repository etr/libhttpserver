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
        frame = std::exchange(frame_, {});
        witness = std::move(witness_);
        resume_ex = std::exchange(resume_ex_, nullptr);
    }
    applied_.store(true, std::memory_order_release);
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
