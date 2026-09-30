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

// Deterministic fake I/O backend of the v3 native engine (architecture
// §3.4, DR-V3-004, TASK-099), plus the io_backend seam it implements
// (the seam itself lives in io_operation.hpp next to the op contract).
//
// One terminal completion: complete(), request_cancel(), and close()
// all funnel through op_state::claim_terminal(), so every submitted
// operation ends with exactly one terminal result -- ok (scripted,
// timer expiry, wake), cancelled (cancel won), or connection_closed
// (close / submit after close). A losing attempt returns false /
// invalid_state and is a no-op. Cancel operations resolve at submit
// time: a pending target is delivered cancelled and the cancel op
// reports ok; a target already terminal makes the cancel op report
// invalid_state.
//
// complete() / request_cancel() are thread-safe and may be invoked from
// any thread in any order -- that is precisely the reordered-completions
// harness. Delivery order is routed through each op's io_connection_owner,
// which serializes per connection. No OS sockets, no syscalls: pure
// in-memory state. Real socket backends arrive with TASK-100.
#if !defined(HTTPSERVER_COMPILATION)
#error "fake_io_backend.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_FAKE_IO_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_FAKE_IO_BACKEND_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <httpserver/detail/io_operation.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {
namespace detail {

class fake_io_backend final : public io_backend {
 public:
    fake_io_backend() = default;

    fake_io_backend(const fake_io_backend&) = delete;
    fake_io_backend& operator=(const fake_io_backend&) = delete;

    // Registers a pending operation and binds its sequence (monotonic,
    // per backend). Submitting the same op twice is a std::logic_error;
    // submitting after close() completes the op immediately with
    // connection_closed -- no silent drops.
    void submit(op_state& op) override;

    http::outcome_code request_cancel(op_state& target) override;

    // ---- deterministic test control surface ---------------------------

    // The ONE scripted terminal completion. False when the op is not
    // pending anymore (already completed, cancelled, or swept by close)
    // or the claim raced a concurrent cancel/close.
    bool complete(op_state& op, io_result result);

    // Completes every pending timer op whose deadline <= now with {ok},
    // in (deadline, sequence) order. Returns how many expired. Ops
    // cancelled concurrently lose the claim and are skipped.
    std::size_t expire_timers(std::chrono::steady_clock::time_point now);

    // Completes every pending wake op with {ok}. Returns the count.
    std::size_t fire_wake();

    // Terminal teardown: claims every pending op with connection_closed
    // and enqueues each to its owner. Returns how many were completed.
    // Later completes are no-ops; a second close returns 0.
    std::size_t close();

    // Operations registered but not yet terminal.
    std::size_t pending_count() const;

 private:
    // Shared cancel path: erases the target from the registry and, on
    // winning the claim, delivers cancelled. True when this call
    // delivered the cancellation.
    bool try_cancel(const std::shared_ptr<op_state>& target);

    // Claims @p state and enqueues @p result to its owner (claim-loser
    // is a no-op).
    void finish_now(const std::shared_ptr<op_state>& state, io_result result);

    mutable std::mutex mu_;
    std::unordered_map<op_state*, std::shared_ptr<op_state>> pending_;
    std::uint64_t next_sequence_ = 1;  // guarded by mu_
    bool closed_ = false;              // guarded by mu_
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_FAKE_IO_BACKEND_HPP_
