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

// Per-connection completion serializer of the v3 native engine
// (architecture §3.4, DR-V3-004, TASK-099). One owner per connection:
// backend delivery order never defines protocol order -- the owner
// applies completion records in arrival order (FIFO), one at a time,
// with no reentrancy, through a coalesced drain job posted on the
// connection's executor.
//
// Contracts:
//   - enqueue() is thread-safe and may be called from any thread, in
//     any order (that is the point: scrambled backend deliveries
//     linearize here). It posts exactly one drain job while none is
//     scheduled; records enqueued while a drain runs are picked up by
//     the running drain, never by a second posted job.
//   - Each record is applied exactly once; combined with the op
//     terminal claim this gives end-to-end exactly-once delivery.
//   - While a record is being applied, no other record starts, even if
//     the resumed task re-enters enqueue().
//   - The destructor applies any still-queued records synchronously
//     with their already-decided results (a claimed completion is never
//     dropped); resumptions are witness-guarded. Normal teardown is
//     backend close() followed by an executor drain, before destruction.
//   - The executor must outlive the owner.
#if !defined(HTTPSERVER_COMPILATION)
#error "io_connection_owner.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_IO_CONNECTION_OWNER_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_CONNECTION_OWNER_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/detail/io_operation.hpp>

namespace httpserver {
namespace detail {

class io_connection_owner final {
 public:
    // @p ex is the serialization executor: drain jobs run on it, and
    // operations awaited on it resume inline during the drain.
    explicit io_connection_owner(executor& ex) noexcept;

    io_connection_owner(const io_connection_owner&) = delete;
    io_connection_owner& operator=(const io_connection_owner&) = delete;
    io_connection_owner(io_connection_owner&&) = delete;
    io_connection_owner& operator=(io_connection_owner&&) = delete;

    ~io_connection_owner();

    // Queues a completion record for application. Called by the backend
    // completion path after op_state won the terminal claim.
    void enqueue(std::shared_ptr<op_state> state, io_result result);

    // Records not yet applied.
    std::size_t pending() const;

    // True while a drain is applying records (reentrancy probe).
    bool draining() const noexcept {
        return draining_.load(std::memory_order_acquire);
    }

 private:
    struct record {
        std::shared_ptr<op_state> state;
        io_result result;
    };

    void drain();

    executor& ex_;
    mutable std::mutex mu_;
    std::deque<record> queue_;      // guarded by mu_
    bool drain_posted_ = false;     // guarded by mu_; coalesces drain jobs
    std::atomic<bool> draining_{false};
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_IO_CONNECTION_OWNER_HPP_
