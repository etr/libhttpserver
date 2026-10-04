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

// TASK-110: the counted-unit accounting behind a native_server drain
// (PRD-V3N-REQ-032, DR-V3-008). One scope per server object; the
// drain ticket holds it alive through a shared_ptr, so a ticket may
// outlive the server and still wait safely.
//
// Units: one per live connection engine (enter at start(), leave at
// finalize()) and one per routed exchange (serve_one's live window).
// The connection unit is the completion condition -- it drops only
// after the engine's loops exited and the writer flushed, so a
// completed drain never truncates a response; the exchange unit makes
// "work counted by its own drain" literally true for the calling
// handler and gives remaining request-level meaning.
//
// The protocol is deliberately agnostic: the HTTP/2 GOAWAY, WebSocket
// close, and QUIC drain paths reuse this same shape.
#if !defined(HTTPSERVER_COMPILATION)
#error "drain_scope.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DRAIN_SCOPE_HPP_
#define SRC_HTTPSERVER_DETAIL_DRAIN_SCOPE_HPP_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/server.hpp>

namespace httpserver {

namespace detail {

// arm() fixes the drain deadline and installs the two hooks the wait
// needs. The counted-thread predicate detects would_deadlock through
// the worker pool's executor slot (never a thread-local marker:
// handler resumptions migrate threads, the installed slot does not).
// The cancel hook is the deadline-expiry hard stop of whatever is
// still counted, fired at most once across concurrent waiters.
//
// Expiry and the last unit leaving arbitrate under the same mutex.
// Its pre-cancel report stays available after all units leave. Hooks run
// outside mu_ and are never touched by a late wait on an idle scope.
class drain_scope final {
 public:
    // Counts one unit up.
    void enter() noexcept;

    // Counts one unit down, notifying waiters at the zero transition.
    // Idempotent at zero.
    void leave() noexcept;

    // Live counted units.
    std::size_t active() const noexcept;

    // One counted unit's lifetime: enter on construction, leave on
    // destruction.
    struct unit {
        explicit unit(drain_scope& scope) noexcept : s(scope) {
            s.enter();
        }

        ~unit() { s.leave(); }

        unit(const unit&) = delete;
        unit& operator=(const unit&) = delete;

        drain_scope& s;
    };

    // Fixes the drain deadline and installs the wait hooks. The server
    // arms once per object (begin_drain's own gate); a re-arm only
    // re-deadlines an unexpired scope.
    void arm(std::chrono::steady_clock::time_point deadline,
             concurrency::unique_function<bool()> on_counted_thread,
             concurrency::unique_function<void()> cancel_remaining) noexcept;

    bool armed() const noexcept;

    // Claims expiry before cancellation; also used by owner watchdogs.
    bool expire_if_due(std::chrono::steady_clock::time_point now) noexcept;

    // Blocking wait bounded by the armed deadline. ok with completed
    // when every unit left; ok with deadline_expired and the pre-cancel
    // remaining count when the deadline passed (the cancel hook fires
    // at most once, on whichever waiter claims the expiry);
    // invalid_state when unarmed; would_deadlock when the calling
    // thread runs work this drain counts (the active_ > 0 gate runs
    // first, so a counted thread between units completes instead).
    http::outcome wait(server::drain_result& out);

 private:
    concurrency::unique_function<void()> claim_expiry_locked(
        std::chrono::steady_clock::time_point now) noexcept;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::size_t active_ = 0;   // guarded by mu_
    bool armed_ = false;       // guarded by mu_
    std::size_t expired_remaining_ = 0;
    bool cancelled_ = false;   // guarded by mu_
    std::chrono::steady_clock::time_point deadline_{};   // guarded by mu_
    concurrency::unique_function<bool()> on_counted_thread_;   // mu_
    concurrency::unique_function<void()> cancel_remaining_;   // mu_
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DRAIN_SCOPE_HPP_
