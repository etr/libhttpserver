/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/concurrency/cancellation.hpp>

namespace httpserver {

auto stop_token::cancellation_awaiter::bind_frame(detail::task_frame_base* frame) noexcept -> cancellation_awaiter& {
    frame_ = frame;
    return *this;
}

auto stop_token::cancellation_awaiter::await_suspend(std::coroutine_handle<> awaiting) -> std::coroutine_handle<> {
    awaiting_ = awaiting;
    if (token_.stop_requested()) {
        // Pre-cancelled: claim and complete inline via symmetric
        // transfer (we are already on the awaiting frame's
        // executor thread). Exactly-once is guaranteed by the
        // claim; no callback is ever registered.
        fired_.exchange(true, std::memory_order_acq_rel);
        return awaiting;
    }
    callback_.emplace(token_, fire{this});
    // Publish the callback, then re-check. Exactly one of the two
    // paths (this registration re-check, or the stop callback on
    // the requesting thread) wins the resume_claimed_ exchange and
    // resumes the waiter:
    //   - the callback saw armed_ == false (it ran before the
    //     store below): it deferred without claiming, so this
    //     re-check wins the exchange and resumes via symmetric
    //     transfer;
    //   - the callback saw armed_ == true: it won the exchange and
    //     posted, so this re-check observes the claim taken and
    //     does nothing.
    // The exchange decides; the two paths can never both act.
    armed_.store(true, std::memory_order_seq_cst);
    if (!fired_.load(std::memory_order_seq_cst)) {
        return std::noop_coroutine();  // callback will post
    }
    if (!resume_claimed_.exchange(true, std::memory_order_acq_rel)) {
        return awaiting;  // stop raced registration: resume here
    }
    return std::noop_coroutine();  // callback claimed and posted
}

auto stop_token::cancellation_awaiter::await_resume() const -> void { throw cancelled_exception(); }

stop_token::cancellation_awaiter::~cancellation_awaiter() {
    // Unregistering here (std::stop_callback's destructor blocks
    // until an in-flight callback completes) keeps a destroyed
    // waiter race-free against a concurrent request_stop(): any
    // posted resumption it leaves behind is a no-op through the
    // frame witness.
}

auto stop_token::cancellation_awaiter::on_stop() noexcept -> void {
    if (!claim()) return;
    // armed_ == false means the registration re-check has not run
    // yet: it will observe fired_ and resume via symmetric
    // transfer, so this path must not claim the resumption.
    if (!armed_.load(std::memory_order_seq_cst)) return;
    if (resume_claimed_.exchange(true, std::memory_order_acq_rel)) {
        return;  // the registration re-check resumed the waiter
    }
    post_resume();
}

auto stop_token::cancellation_awaiter::post_resume() noexcept -> void {
    executor* const ex = frame_ ? frame_->frame_executor() : current_executor();
    const auto witness = frame_ ? frame_->frame_witness_ptr() : nullptr;
    std::coroutine_handle<> frame = awaiting_;
    // Always posted: no waiter is resumed synchronously inside
    // request_stop(), which is the thread running this callback.
    if (ex != nullptr) {
        try {
            ex->post([witness, frame] {
                if (witness) {
                    detail::guarded_resume(witness, frame);
                } else {
                    frame.resume();
                }
            });
        } catch (...) {
        }
    } else if (witness) {
        detail::guarded_resume(witness, frame);
    } else {
        frame.resume();
    }
}

}  // namespace httpserver
