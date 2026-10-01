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

// TASK-106: the HTTP/1 engine's detail::body_source adapter (TASK-103
// seam; PRD-V3N-REQ-021). NOT part of the installed surface.
//
// Turns engine-side socket reads into the pull/park seam the exchange's
// body_reader consumes. feed() runs the incremental decoder under the
// adapter lock and — exactly once per waiter, via body_wait::complete
// (a CAS plus a posted, witness-guarded resumption; no waiter code ever
// runs under the lock) — wakes a parked reader on the FIRST of: a
// framing failure, newly staged bytes, or the message end. park()
// mirrors the immediate-completion table (already failed, already
// staged, already complete, stop requested — else register; at most one
// waiter per source); the stop arm is engine-driven: the engine calls
// cancel_parked() after disconnecting the exchange, so a read parked
// before the disconnect wakes cancelled and one parking after sees the
// sticky stop at park's own table. The engine never re-feeds the bytes
// feed() reports consumed.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_body_source.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_BODY_SOURCE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_BODY_SOURCE_HPP_

#include <cstddef>
#include <mutex>
#include <string_view>

#include <httpserver/body_reader.hpp>
#include <httpserver/detail/http1_body_decoder.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

class http1_body_source final : public body_source {
 public:
    http1_body_source(const http1_body_mode& mode, http1_body_budget budget)
        : decoder_(mode, budget) { }

    http1_body_source(const http1_body_source&) = delete;
    http1_body_source& operator=(const http1_body_source&) = delete;
    http1_body_source(http1_body_source&&) = delete;
    http1_body_source& operator=(http1_body_source&&) = delete;

    // Engine-side entry: frames a prefix of `wire` and wakes a parked
    // waiter when the decoder failed, staged new bytes, or completed
    // the message. Returns the consumed count; the engine re-feeds the
    // rest (nothing was consumed when backpressure blocks the decode).
    std::size_t feed(std::string_view wire) {
        body_wait* waiter = nullptr;
        body_wake wake = body_wake::data;
        std::size_t consumed = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            const std::size_t staged_before = decoder_.staged_bytes();
            const http1_body_progress p = decoder_.decode(wire);
            consumed = p.consumed;
            if (waiter_ != nullptr) {
                if (p.kind == http1_body_decode::failed) {
                    waiter = take_waiter_locked();
                    wake = body_wake::failed;
                } else if (decoder_.staged_bytes() > staged_before) {
                    waiter = take_waiter_locked();
                    wake = body_wake::data;
                } else if (p.kind == http1_body_decode::complete) {
                    waiter = take_waiter_locked();
                    wake = body_wake::end;
                }
            }
        }
        if (waiter != nullptr) waiter->complete(wake);
        return consumed;
    }

    // Engine-side disconnect arm: wakes the parked waiter (if any) as
    // cancelled. Call AFTER the exchange's disconnect made the stop
    // sticky — park() checks-and-registers in one critical section, so
    // the pairing cannot miss a read (one parked earlier is taken here;
    // one parking later completes inline at park's own table).
    void cancel_parked() {
        body_wait* waiter = nullptr;
        {
            std::lock_guard<std::mutex> lock(mu_);
            waiter = take_waiter_locked();
        }
        if (waiter != nullptr) waiter->complete(body_wake::cancelled);
    }

    bool failed() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return !decoder_.failure().ok();
    }

    bool message_complete() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return decoder_.message_complete();
    }

    std::size_t staged_bytes() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return decoder_.staged_bytes();
    }

    // Close posture of the recorded failure (rejected body mode or a
    // framing failure); none while the body is healthy.
    http1_close_policy close_policy() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return decoder_.close_policy();
    }

    // True iff a read is parked right now (test/engine observability).
    bool parked() const noexcept {
        std::lock_guard<std::mutex> lock(mu_);
        return waiter_ != nullptr;
    }

    // -- detail::body_source seam ----------------------------------------

    body_pull_result pull(std::span<std::byte> into) override {
        std::lock_guard<std::mutex> lock(mu_);
        return decoder_.pull(into);
    }

    // Trailers are final once a pull returned end; after that no feed
    // can mutate them (the decoder is complete), so the unlocked
    // reference is stable. Mirrors the scripted-test fake.
    const http::fields& trailers() const noexcept override {
        return decoder_.trailers();
    }

    // Meaningful once a pull returned failed; the outcome is sticky.
    const http::outcome& failure() const noexcept override {
        return decoder_.failure();
    }

    void park(body_wait& wait) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (!decoder_.failure().ok()) {
            wait.complete(body_wake::failed);
            return;
        }
        if (decoder_.staged_bytes() > 0) {
            wait.complete(body_wake::data);
            return;
        }
        if (decoder_.message_complete()) {
            wait.complete(body_wake::end);
            return;
        }
        if (wait.cancel_token().stop_requested()) {
            wait.complete(body_wake::cancelled);
            return;
        }
        waiter_ = &wait;  // at most one waiter per source
    }

    void unpark(body_wait& wait) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (waiter_ == &wait) waiter_ = nullptr;
    }

 private:
    // Forgets the registered waiter under the lock; the caller
    // completes it after unlocking.
    body_wait* take_waiter_locked() noexcept {
        body_wait* waiter = waiter_;
        waiter_ = nullptr;
        return waiter;
    }

    mutable std::mutex mu_;
    http1_body_decoder decoder_;
    body_wait* waiter_ = nullptr;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_BODY_SOURCE_HPP_
