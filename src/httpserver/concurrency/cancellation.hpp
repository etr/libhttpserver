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

#ifndef SRC_HTTPSERVER_CONCURRENCY_CANCELLATION_HPP_
#define SRC_HTTPSERVER_CONCURRENCY_CANCELLATION_HPP_

// Cancellation primitives of the v3 concurrency core (architecture §3.1,
// PRD-V3N-REQ-025/031). stop_source is the single request point;
// stop_token is the copyable fan-out handle. request_stop() is
// idempotent, never blocks, and returns while the caller is still inside
// a handler: no waiter is ever resumed synchronously inside
// request_stop() — resumptions are posted to each waiter's executor.
//
// co_await token.cancelled() suspends the task until stop is requested;
// it then completes exactly once by throwing the cancelled_exception
// marker, which the task's promise converts into the typed terminal
// outcome http::outcome_code::cancelled (never a propagating error).

#include <atomic>
#include <coroutine>
#include <optional>
#include <stop_token>
#include <utility>

#include <httpserver/concurrency/task.hpp>

namespace httpserver {

class stop_token;

// The single stop-request point. Copying is deliberately removed: one
// exchange, one requester. Tokens fan out by copying from get_token().
class stop_source {
 public:
    stop_source() = default;
    stop_source(stop_source&&) = default;
    stop_source& operator=(stop_source&&) = default;
    stop_source(const stop_source&) = delete;
    stop_source& operator=(const stop_source&) = delete;

    // Requests stop. Returns true if this call won the race (first
    // request); false afterwards. Idempotent and non-blocking; returns
    // while the caller is inside a handler (DR-V3-008: handler-safe stop
    // initiation returns immediately).
    bool request_stop() noexcept { return source_.request_stop(); }

    bool stop_requested() const noexcept { return source_.stop_requested(); }
    bool stop_possible() const noexcept { return source_.stop_possible(); }

    stop_token get_token() const noexcept;

 private:
    std::stop_source source_;
};

// Copyable fan-out handle observing a stop_source.
class stop_token {
 public:
    stop_token() noexcept = default;

    bool stop_requested() const noexcept { return token_.stop_requested(); }
    bool stop_possible() const noexcept { return token_.stop_possible(); }

    // Awaitable completing the awaiting task exactly once with the typed
    // cancelled outcome. Completing a pre-cancelled token happens on the
    // awaiting frame's own thread (symmetric transfer at registration
    // time); completing after a later request_stop() is posted to the
    // awaiting frame's executor — never run on the requesting thread.
    class cancellation_awaiter final {
     public:
        explicit cancellation_awaiter(std::stop_token token) noexcept
            : token_(std::move(token)) { }

        cancellation_awaiter& bind_frame(detail::task_frame_base* frame) noexcept {
            frame_ = frame;
            return *this;
        }

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) {
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

        // Resumption only ever happens through a fired path.
        void await_resume() const {
            throw cancelled_exception();
        }

        ~cancellation_awaiter() {
            // Unregistering here (std::stop_callback's destructor blocks
            // until an in-flight callback completes) keeps a destroyed
            // waiter race-free against a concurrent request_stop(): any
            // posted resumption it leaves behind is a no-op through the
            // frame witness.
        }

     private:
        struct fire {
            cancellation_awaiter* self;
            void operator()() const { self->on_stop(); }
        };

        bool claim() noexcept {
            return !fired_.exchange(true, std::memory_order_acq_rel);
        }

        void on_stop() noexcept {
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

        void post_resume() noexcept {
            executor* const ex =
                frame_ ? frame_->frame_executor() : current_executor();
            const auto witness =
                frame_ ? frame_->frame_witness_ptr() : nullptr;
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

        std::stop_token token_;
        std::optional<std::stop_callback<fire>> callback_;
        detail::task_frame_base* frame_ = nullptr;
        std::coroutine_handle<> awaiting_;
        std::atomic<bool> fired_{false};
        std::atomic<bool> armed_{false};
        // Single-resumer handoff between the registration re-check and the
        // stop callback: exactly one exchange wins and resumes the waiter.
        std::atomic<bool> resume_claimed_{false};
    };

    [[nodiscard]] cancellation_awaiter cancelled() const noexcept {
        return cancellation_awaiter(token_);
    }

 private:
    friend class stop_source;

    explicit stop_token(std::stop_token token) noexcept
        : token_(std::move(token)) { }

    std::stop_token token_;
};

inline stop_token stop_source::get_token() const noexcept {
    return stop_token(source_.get_token());
}

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CONCURRENCY_CANCELLATION_HPP_
