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

// TASK-107: ordered per-connection response persistence
// (PRD-V3N-REQ-026/027, DR-V3-006). NOT part of the installed surface.
//
// One http1_response_outbox per connection: a FIFO of per-response
// staging slots appended in open() order. Each slot serializes through
// its own framer into its own byte buffer; copy_front/consume_front
// touch only the front slot. A response that completes early still
// waits its turn — bytes physically cannot interleave, which is the
// mechanism behind the corpus's pipelined_keepalive case.
//
// Bounding: one shared max_queue_bytes across all slots. push/push_end
// copy as much as fits (chunk-size-aware through the framer's output
// bound) and report full when the budget is exhausted; the response
// writer parks. Room appears only from consume_front (or abandon).
// A serialized head larger than max_head_bytes fails the sink
// immediately (limit_exceeded) — never a park on impossible room.
// Deliberate v3.0 delta: the end marker appends after the room check
// and may overshoot the budget by its (trusted, handler-supplied)
// trailer size; the budget's backpressure purpose is unaffected.
//
// The outbox is also the connection's detail::body_sink provider: each
// open() returns an http1_response_sink the exchange's response writer
// pushes through. TASK-104's generic bounded queue is the test fake;
// this class is the HTTP/1 engine's real one.
//
// Threading: one mutex guards the slots, the budget counter, and the
// per-sink waiter tables; completion goes through body_write_wait::
// complete — a CAS plus a posted, witness-guarded resumption — so no
// waiter code ever runs under the lock (the scripted-test fake
// establishes the same pattern). On room freed every parked sink wakes
// (bounded thundering herd accepted at v3.0 scale; fairness is a later
// gate).

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_response_outbox.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_OUTBOX_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_OUTBOX_HPP_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_operation.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_writer.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/detail/resource_charge.hpp>

namespace httpserver {

namespace detail {

class http1_response_outbox;

// Per-response staging slot and body_sink seam. Created only by
// http1_response_outbox::open(); one per response, in request order.
// The engine (or the test standing in for it) drives start()/interim();
// the exchange's response writer drives the body_sink seam.
class http1_response_sink final : public detail::body_sink {
 public:
    // Commits the response head: computes the framing mode once and
    // serializes the head bytes into this slot's buffer. A rejected
    // mode or an oversized head fails the sink sticky and queues
    // nothing.
    http::outcome start(const http::request_head& request,
                        const http::status& s, const http::fields& f,
                        const http1_response_framer::clock_source& clock =
                            {});

    // Trusted WebSocket-only head. Fixed hop-by-hop fields, validated
    // accept/subprotocol, no generic bypass of framer sanitization.
    // Precommit refusal leaves this slot usable for ordinary HTTP.
    http::outcome commit_upgrade(std::string_view accept, std::string_view protocol);

    // Appends an interim head ahead of the final head in this slot.
    // Legal before start(): the 100-continue interim precedes the final
    // head by definition, so the framer is created lazily here.
    http::outcome interim(std::uint16_t code);

    // -- detail::body_sink seam (the exchange's writer calls these) ----

    body_push_result push(std::span<const std::byte> from) override;
    body_push_result push_end(const http::fields& trailers) override;

    // Meaningful once a push returned failed; the outcome is sticky.
    const http::outcome& failure() const noexcept override {
        return failure_;
    }

    void park(body_write_wait& wait) override;
    void unpark(body_write_wait& wait) override;

    // True while a write is parked on this slot (test/engine
    // observability).
    bool parked() const;

 private:
    friend class http1_response_outbox;
    friend struct http1_outbox_test_access;

    http1_response_sink(http1_response_outbox& owner, stop_token cancel)
        : owner_(&owner), cancel_(std::move(cancel)) { }

    http1_response_sink(const http1_response_sink&) = delete;
    http1_response_sink& operator=(const http1_response_sink&) = delete;

    // Marks the sink failed (sticky) and returns the reason.
    http::outcome fail_locked(const http::outcome& reason);

    http1_response_outbox* owner_;
    stop_token cancel_;
    std::unique_ptr<http1_response_framer> framer_;
    std::string bytes_;       // serialized interim + head + body bytes
    std::size_t storage_units_ = 0;  // retained buffer allocation, including unused capacity
    std::size_t offset_ = 0;  // consumed prefix (front slot only)
    std::uint64_t sequence_ = 0;  // request order bookkeeping
    http::outcome failure_;
    body_write_wait* waiter_ = nullptr;  // at most one parked write
    bool started_ = false;  // start() ran (interim alone does not count)
    bool ended_ = false;
    bool failed_ = false;
};

struct http1_outbox_budget {
    std::size_t max_queue_bytes = 4194304;  // response_queue_bytes default
    std::size_t max_head_bytes = 65536;     // serialized head bound

    // Projects the queue and head resources out of a budget_limits
    // scope (the http1_head_budget convention, response side).
    static http1_outbox_budget from_budget_limits(
        const server::budget_limits& limits) noexcept;
};

// The ordered per-connection outbox. Not copyable or movable: slots
// borrow the outbox through raw pointers and waiters register with it.
class http1_response_outbox {
 public:
    explicit http1_response_outbox(
        http1_outbox_budget budget = http1_outbox_budget(), server::resource_budget scope = {});

    http1_response_outbox(const http1_response_outbox&) = delete;
    http1_response_outbox& operator=(const http1_response_outbox&) = delete;

    // Appends a staging slot for the response arriving at `sequence`
    // and returns its sink. Call order is the request order; the slot
    // drains strictly in that order. `cancel` is the exchange's
    // disconnect token: a park on this sink completes cancelled once it
    // fires.
    http1_response_sink& open(std::uint64_t sequence, stop_token cancel);

    // Copies the front slot's next bytes into `dest`; 0 when the outbox
    // is empty or the front slot has nothing buffered yet.
    std::size_t copy_front(std::span<std::byte> dest) const;

    // Consumes up to `n` front bytes. The front slot pops once it is
    // ended and fully consumed. Consumed bytes free budget: every
    // parked sink's waiter completes with freed (completion itself is
    // safe under the lock — a CAS plus a posted resumption).
    std::size_t consume_front(std::size_t n);

    // True when the front slot is ended and fully consumed (its next
    // consume_front pops it).
    bool front_complete() const;

    // True when the front slot FAILED (a refused head, an abandoned
    // connection): nothing more will ever be queued to it, so a
    // waiting writer is terminal -- its already-buffered bytes still
    // flush in order first (copy_front serves them as usual), but the
    // drain must not wait for an end marker that can no longer arrive
    // (TASK-118: the engine-refused head must close the connection).
    bool front_failed() const;

    bool empty() const;

    // Bytes currently buffered across all slots (the shared budget's
    // usage).
    std::size_t queued_bytes() const;

    // Drains the front slot towards `backend`: copies into a
    // coroutine-frame buffer, submits one write_operation at a time,
    // consumes exactly the transferred prefix, and loops until the
    // outbox empties or the front has nothing buffered. Non-ok io
    // results return as-is — TASK-108 owns retry and close. Never
    // parks: a still-open front with no buffered bytes ends the drain
    // (the connection loop resumes it when the handler produced more).
    task<io_result> write_front(io_backend& backend,
                                io_connection_owner& owner,
                                std::uint64_t connection);

    // All transport and route loops have retired before this terminal cleanup.
    void retire();

    // Connection teardown: every sink — parked or not — fails with
    // connection_closed, parked waiters complete with failed, and slots
    // opened later fail at open(). Idempotent.
    void abandon();

 private:
    friend class http1_response_sink;
    friend struct http1_outbox_test_access;

    bool field_bound(const http::fields& fields, std::size_t fixed,
                     std::size_t& bound) const;

    // Preallocate the whole candidate before serialization and keep the
    // old allocation charged until its replacement has copied the bytes.
    // Geometric growth preserves amortized append cost. Allocator-rounded
    // excess must also be admitted before copying or publishing the buffer;
    // refusal or an exception destroys the candidate and rolls back its charge.
    // Inline capacity is conservatively included for nonempty buffers.
    bool reserve_buffer_locked(http1_response_sink& sink, std::size_t size);

    void release_buffer_locked(http1_response_sink& sink);

    // Shared push path of the body_sink seam. Partial copies are the
    // norm: the framer takes the remaining budget as its output bound
    // and the copied delta counts against the logical queue budget.
    // Shared storage separately tracks the full retained allocation.
    body_push_result push_into(http1_response_sink& sink,
                               std::span<const std::byte> from);

    body_push_result end_slot(http1_response_sink& sink,
                              const http::fields& trailers);

    // body_sink::park: completes immediately on the first of a recorded
    // failure, available room, or a fired stop token; else registers
    // the (single) waiter.
    void park_sink(http1_response_sink& sink, body_write_wait& wait);

    void unpark_sink(http1_response_sink& sink, body_write_wait& wait);

    // Collects every registered waiter under the lock and clears the
    // registrations.
    void take_all_waiters_locked(std::vector<body_write_wait*>& out);

    static void wake_all(const std::vector<body_write_wait*>& waiters,
                         body_room room);

    static body_push_result failed_result() {
        return body_push_result{body_push::failed, 0};
    }

    http1_outbox_budget budget_;
    resource_charge storage_charge_;
    mutable std::mutex mu_;
    std::deque<std::unique_ptr<http1_response_sink>> slots_;
    std::size_t queued_bytes_ = 0;  // guarded by mu_
    bool abandoned_ = false;        // guarded by mu_
};

// -- http1_response_sink definitions (they need the complete outbox) --

bool valid_upgrade_fields(std::string_view accept, std::string_view protocol);







inline body_push_result http1_response_sink::push(
    std::span<const std::byte> from) {
    return owner_->push_into(*this, from);
}

inline body_push_result http1_response_sink::push_end(
    const http::fields& trailers) {
    return owner_->end_slot(*this, trailers);
}

inline void http1_response_sink::park(body_write_wait& wait) {
    owner_->park_sink(*this, wait);
}

inline void http1_response_sink::unpark(body_write_wait& wait) {
    owner_->unpark_sink(*this, wait);
}





}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_OUTBOX_HPP_
