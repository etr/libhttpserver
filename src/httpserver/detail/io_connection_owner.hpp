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
#include <utility>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/detail/io_operation.hpp>

namespace httpserver {
namespace detail {

class datagram_sink {
 public:
    virtual ~datagram_sink() = default;
    virtual void on_datagram(std::shared_ptr<const io_datagram> packet) noexcept = 0;
};
// A route generation owns its sink until all queued/active deliveries retire.
struct datagram_delivery {
    explicit datagram_delivery(std::shared_ptr<datagram_sink> target) : sink(std::move(target)) { }
    std::shared_ptr<datagram_sink> sink;
    std::atomic<bool> retired{false};
};
enum class datagram_enqueue_code { queued, retired, full };

class io_connection_owner final {
    struct shared_state;
 public:
    // A lifetime-safe enqueue port; retained routes never dereference an owner.
    class datagram_port {
     public:
        datagram_enqueue_code enqueue(std::shared_ptr<datagram_delivery> delivery,
                                      std::shared_ptr<const io_datagram> packet) const;
     private:
        friend class io_connection_owner;
        datagram_port(std::weak_ptr<shared_state> state, executor* ex) : state_(std::move(state)), ex_(ex) { }
        std::weak_ptr<shared_state> state_;
        executor* ex_;
    };
    datagram_port datagrams() const { return datagram_port(state_, &ex_); }

    // @p ex is the serialization executor: drain jobs run on it, and
    // operations awaited on it resume inline during the drain.
    explicit io_connection_owner(executor& ex, std::size_t packet_count = k_udp_pending_operations,
                                 std::size_t packet_bytes = k_udp_pending_bytes) noexcept;

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
        return state_->draining.load(std::memory_order_acquire);
    }

 private:
    struct record {
        std::shared_ptr<op_state> state;
        io_result result;
        std::shared_ptr<datagram_delivery> delivery;
        std::shared_ptr<const io_datagram> packet;
    };

    // Queue, coalescing flag, and drain marker live in a block shared
    // with the posted drain jobs. TASK-108: a wake is globally
    // coalesced across connections, so a completion can arrive for a
    // connection whose drain job already ran and whose engine is
    // unwinding; that late drain job must find live (empty) state
    // instead of freed memory. By the time the owner dies the backend
    // released the connection (every op claimed), so a post-mortem
    // drain is always a no-op sweep.
    struct shared_state {
        // Serializes enqueue posting with teardown; recursive for inline executors.
        std::recursive_mutex posting_mu;
        mutable std::mutex mu;
        std::deque<record> queue;
        bool alive = true;
        std::size_t packet_count = 0;
        std::size_t packet_bytes = 0;
        std::size_t max_packet_count = k_udp_pending_operations;
        std::size_t max_packet_bytes = k_udp_pending_bytes;
        bool drain_posted = false;  // guarded by mu; coalesces jobs
        std::atomic<bool> draining{false};
    };

    static void apply_record(record entry, const std::shared_ptr<shared_state>& pending);
    static void drain(std::shared_ptr<shared_state> pending);

    executor& ex_;
    std::shared_ptr<shared_state> state_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_IO_CONNECTION_OWNER_HPP_
