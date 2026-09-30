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

    http1_response_sink(http1_response_outbox& owner, stop_token cancel)
        : owner_(&owner), cancel_(std::move(cancel)) { }

    http1_response_sink(const http1_response_sink&) = delete;
    http1_response_sink& operator=(const http1_response_sink&) = delete;

    // Marks the sink failed (sticky) and returns the reason.
    http::outcome fail_locked(const http::outcome& reason) {
        failed_ = true;
        failure_ = reason;
        return reason;
    }

    http1_response_outbox* owner_;
    stop_token cancel_;
    std::unique_ptr<http1_response_framer> framer_;
    std::string bytes_;       // serialized interim + head + body bytes
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
        const server::budget_limits& limits) noexcept {
        http1_outbox_budget budget;
        budget.max_queue_bytes =
            limits.get(server::resource::response_queue_bytes);
        budget.max_head_bytes = limits.get(server::resource::header_bytes);
        return budget;
    }
};

// The ordered per-connection outbox. Not copyable or movable: slots
// borrow the outbox through raw pointers and waiters register with it.
class http1_response_outbox {
 public:
    explicit http1_response_outbox(
        http1_outbox_budget budget = http1_outbox_budget())
        : budget_(budget) { }

    http1_response_outbox(const http1_response_outbox&) = delete;
    http1_response_outbox& operator=(const http1_response_outbox&) = delete;

    // Appends a staging slot for the response arriving at `sequence`
    // and returns its sink. Call order is the request order; the slot
    // drains strictly in that order. `cancel` is the exchange's
    // disconnect token: a park on this sink completes cancelled once it
    // fires.
    http1_response_sink& open(std::uint64_t sequence, stop_token cancel) {
        std::lock_guard<std::mutex> lock(mu_);
        // The sink constructor is private to the outbox (engine-only
        // seam), so the unique_ptr adopts a direct new here.
        slots_.emplace_back(std::unique_ptr<http1_response_sink>(
            new http1_response_sink(*this, std::move(cancel))));
        http1_response_sink& sink = *slots_.back();
        sink.sequence_ = sequence;
        if (abandoned_) {
            static_cast<void>(sink.fail_locked(http::outcome(
                http::outcome_code::connection_closed,
                "http1_response_outbox: connection abandoned")));
        }
        return sink;
    }

    // Copies the front slot's next bytes into `dest`; 0 when the outbox
    // is empty or the front slot has nothing buffered yet.
    std::size_t copy_front(std::span<std::byte> dest) const {
        std::lock_guard<std::mutex> lock(mu_);
        if (slots_.empty()) return 0;
        const http1_response_sink& front = *slots_.front();
        const std::size_t available = front.bytes_.size() - front.offset_;
        const std::size_t n = std::min(dest.size(), available);
        if (n > 0) {
            std::memcpy(dest.data(), front.bytes_.data() + front.offset_, n);
        }
        return n;
    }

    // Consumes up to `n` front bytes. The front slot pops once it is
    // ended and fully consumed. Consumed bytes free budget: every
    // parked sink's waiter completes with freed (completion itself is
    // safe under the lock — a CAS plus a posted resumption).
    std::size_t consume_front(std::size_t n) {
        std::vector<body_write_wait*> woken;
        std::size_t consumed = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (slots_.empty()) return 0;
            http1_response_sink& front = *slots_.front();
            consumed = std::min(n, front.bytes_.size() - front.offset_);
            front.offset_ += consumed;
            queued_bytes_ -= consumed;
            if (front.ended_ && front.offset_ == front.bytes_.size()) {
                // Popped even for a zero consume: the call doubles as
                // the "advance past the completed front" step.
                slots_.pop_front();
            }
            if (consumed > 0) take_all_waiters_locked(woken);
        }
        wake_all(woken, body_room::freed);
        return consumed;
    }

    // True when the front slot is ended and fully consumed (its next
    // consume_front pops it).
    bool front_complete() const {
        std::lock_guard<std::mutex> lock(mu_);
        if (slots_.empty()) return false;
        const http1_response_sink& front = *slots_.front();
        return front.ended_ && front.offset_ == front.bytes_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mu_);
        return slots_.empty();
    }

    // Bytes currently buffered across all slots (the shared budget's
    // usage).
    std::size_t queued_bytes() const {
        std::lock_guard<std::mutex> lock(mu_);
        return queued_bytes_;
    }

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

    // Connection teardown: every sink — parked or not — fails with
    // connection_closed, parked waiters complete with failed, and slots
    // opened later fail at open(). Idempotent.
    void abandon() {
        std::vector<body_write_wait*> woken;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (abandoned_) return;
            abandoned_ = true;
            for (const std::unique_ptr<http1_response_sink>& slot : slots_) {
                static_cast<void>(slot->fail_locked(http::outcome(
                    http::outcome_code::connection_closed,
                    "http1_response_outbox: connection abandoned")));
                if (slot->waiter_ != nullptr) {
                    woken.push_back(slot->waiter_);
                    slot->waiter_ = nullptr;
                }
            }
        }
        wake_all(woken, body_room::failed);
    }

 private:
    friend class http1_response_sink;

    // Shared push path of the body_sink seam. Partial copies are the
    // norm: the framer takes the remaining budget as its output bound
    // and the copied delta is what counts against the budget.
    body_push_result push_into(http1_response_sink& sink,
                               std::span<const std::byte> from) {
        std::lock_guard<std::mutex> lock(mu_);
        if (sink.failed_) return failed_result();
        if (sink.framer_ == nullptr) {
            static_cast<void>(sink.fail_locked(http::outcome(
                http::outcome_code::invalid_state,
                "http1_response_sink: body push before the response"
                " head")));
            return failed_result();
        }
        const std::size_t room = budget_.max_queue_bytes - queued_bytes_;
        if (room == 0) return body_push_result{body_push::full, 0};
        const std::size_t before = sink.bytes_.size();
        const http::outcome framed =
            sink.framer_->push_body(sink.bytes_, from, room);
        const std::size_t copied = sink.bytes_.size() - before;
        if (!framed.ok()) {
            static_cast<void>(sink.fail_locked(framed));
            return body_push_result{body_push::failed, 0};
        }
        if (copied == 0) return body_push_result{body_push::full, 0};
        queued_bytes_ += copied;
        return body_push_result{body_push::accepted, copied};
    }

    body_push_result end_slot(http1_response_sink& sink,
                              const http::fields& trailers) {
        std::lock_guard<std::mutex> lock(mu_);
        if (sink.failed_) return failed_result();
        if (sink.framer_ == nullptr) {
            static_cast<void>(sink.fail_locked(http::outcome(
                http::outcome_code::invalid_state,
                "http1_response_sink: body end before the response"
                " head")));
            return failed_result();
        }
        const std::size_t room = budget_.max_queue_bytes - queued_bytes_;
        if (room == 0) return body_push_result{body_push::full, 0};
        const std::size_t before = sink.bytes_.size();
        const http::outcome framed =
            sink.framer_->finish_body(sink.bytes_, trailers);
        if (!framed.ok()) {
            static_cast<void>(sink.fail_locked(framed));
            return body_push_result{body_push::failed, 0};
        }
        queued_bytes_ += sink.bytes_.size() - before;
        sink.ended_ = true;
        return body_push_result{body_push::accepted, 0};
    }

    // body_sink::park: completes immediately on the first of a recorded
    // failure, available room, or a fired stop token; else registers
    // the (single) waiter.
    void park_sink(http1_response_sink& sink, body_write_wait& wait) {
        std::lock_guard<std::mutex> lock(mu_);
        if (sink.failed_) {
            wait.complete(body_room::failed);
            return;
        }
        if (queued_bytes_ < budget_.max_queue_bytes) {
            wait.complete(body_room::freed);
            return;
        }
        if (sink.cancel_.stop_requested()) {
            wait.complete(body_room::cancelled);
            return;
        }
        sink.waiter_ = &wait;
    }

    void unpark_sink(http1_response_sink& sink, body_write_wait& wait) {
        std::lock_guard<std::mutex> lock(mu_);
        if (sink.waiter_ == &wait) sink.waiter_ = nullptr;
    }

    // Collects every registered waiter under the lock and clears the
    // registrations.
    void take_all_waiters_locked(std::vector<body_write_wait*>& out) {
        for (const std::unique_ptr<http1_response_sink>& slot : slots_) {
            if (slot->waiter_ != nullptr) {
                out.push_back(slot->waiter_);
                slot->waiter_ = nullptr;
            }
        }
    }

    static void wake_all(const std::vector<body_write_wait*>& waiters,
                         body_room room) {
        for (body_write_wait* wait : waiters) {
            wait->complete(room);
        }
    }

    static body_push_result failed_result() {
        return body_push_result{body_push::failed, 0};
    }

    http1_outbox_budget budget_;
    mutable std::mutex mu_;
    std::deque<std::unique_ptr<http1_response_sink>> slots_;
    std::size_t queued_bytes_ = 0;  // guarded by mu_
    bool abandoned_ = false;        // guarded by mu_
};

// -- http1_response_sink definitions (they need the complete outbox) --

inline http::outcome http1_response_sink::start(
    const http::request_head& request, const http::status& s,
    const http::fields& f,
    const http1_response_framer::clock_source& clock) {
    std::lock_guard<std::mutex> lock(owner_->mu_);
    if (failed_) return failure_;
    if (started_) {
        return http::outcome(
            http::outcome_code::invalid_state,
            "http1_response_sink: response head already started");
    }
    if (framer_ == nullptr) {
        framer_ = std::make_unique<http1_response_framer>(clock);
    }
    started_ = true;
    const http::outcome head = framer_->start_head(bytes_, request, s, f);
    if (!head.ok()) {
        bytes_.clear();
        return fail_locked(head);
    }
    if (bytes_.size() > owner_->budget_.max_head_bytes) {
        bytes_.clear();
        return fail_locked(http::outcome(
            http::outcome_code::limit_exceeded,
            "http1_response_sink: serialized response head exceeds the"
            " head budget"));
    }
    owner_->queued_bytes_ += bytes_.size();
    return http::outcome::okay();
}

inline http::outcome http1_response_sink::interim(std::uint16_t code) {
    std::lock_guard<std::mutex> lock(owner_->mu_);
    if (failed_) return failure_;
    if (framer_ == nullptr) {
        // Interims carry no Date, so the default (clock-less) framer is
        // the right serializer here.
        framer_ = std::make_unique<http1_response_framer>();
    }
    const std::size_t before_bytes = bytes_.size();
    const http::outcome appended = framer_->interim_head(bytes_, code);
    if (!appended.ok()) return appended;
    owner_->queued_bytes_ += bytes_.size() - before_bytes;
    return http::outcome::okay();
}

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

inline bool http1_response_sink::parked() const {
    std::lock_guard<std::mutex> lock(owner_->mu_);
    return waiter_ != nullptr;
}

inline task<io_result> http1_response_outbox::write_front(
    io_backend& backend, io_connection_owner& owner,
    std::uint64_t connection) {
    std::array<std::byte, 4096> buffer;
    io_result total;
    for (;;) {
        std::size_t n = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (slots_.empty()) co_return total;
            const http1_response_sink& front = *slots_.front();
            const std::size_t available =
                front.bytes_.size() - front.offset_;
            if (available == 0) co_return total;
            n = std::min(buffer.size(), available);
            std::memcpy(buffer.data(), front.bytes_.data() + front.offset_,
                        n);
        }
        write_operation op(owner, connection,
                           std::span<const std::byte>(buffer.data(), n));
        op.submit(backend);
        const io_result piece = co_await std::move(op);
        if (piece.code != http::outcome_code::ok) co_return piece;
        consume_front(piece.transferred);
        total.transferred += piece.transferred;
    }
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_OUTBOX_HPP_
