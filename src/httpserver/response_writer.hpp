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

#ifndef SRC_HTTPSERVER_RESPONSE_WRITER_HPP_
#define SRC_HTTPSERVER_RESPONSE_WRITER_HPP_

// Bounded streaming response-body delivery for the v3 exchange
// (architecture §3.1, DR-V3-003, PRD-V3N-REQ-025/026/027/037). A
// handler that commits a streaming response head with
// exchange::start_response() writes the body through
// exchange::writer(), a response_writer:
//
//   - write(chunk) streams one chunk into the engine's bounded output
//     queue; exactly one write (or finish) may be outstanding at a
//     time; when write resolves ok the whole chunk was accepted;
//   - finish(trailers) ends the body; the trailers ride the final
//     framing and the writer closes;
//   - a write parked on a full queue wakes on the first of queue room,
//     engine failure, or exchange disconnect — writes await bounded
//     queue capacity, never peer acknowledgement (PRD-V3N-REQ-027).
//
// The engine side of the seam is detail::body_sink, declared in this
// public header for the same reason detail::exchange_sink is: the
// writer's inline methods invoke it and a public header may not
// include a private one. It is engine plumbing, not consumer surface.
// The push model inverts the request side (body_reader): the handler
// produces, the engine drains. A slow transport fills the bounded
// queue; the parked write IS the backpressure. Room appears only while
// the engine drains its queue towards the transport.
//
// Threading contract: writes run on the handler's executor thread (no
// locks); a parked write's completion may arrive from any thread and
// is posted to the writer's executor, mirroring the resume-signal
// rules. Lifetime: write()'s span is consumed before the task resolves
// (the engine copies); moving an exchange with an outstanding write is
// a contract violation (writes happen between moves).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <coroutine>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

// Typed result of one response body write. ok: the whole chunk was
// accepted into the engine's bounded output queue (accepted == chunk
// size); cancelled: the exchange disconnected mid-write (accepted is
// the bytes queued before the wake, diagnostic only); every failure
// carries its typed outcome and accepts nothing further.
struct body_write {
    http::outcome status;
    std::size_t accepted = 0;
};

// Typed result of finish(): ok ends the body (trailers ride the final
// framing); failures are the same typed set as body_write.
struct body_finish {
    http::outcome status;
};

namespace detail {

class body_write_wait;

// Verdict of one body_sink::push() / push_end().
enum class body_push : std::uint8_t {
    accepted,  // bytes copied into the engine's bounded output queue
    full,      // queue at capacity; the writer must park
    failed,    // transport/protocol failure; see body_sink::failure()
};

// What a parked body_write_wait completes with.
enum class body_room : std::uint8_t {
    freed,      // output queue room freed; re-push
    failed,
    cancelled,  // the exchange's stop fired before completion
};

// A push verdict plus the byte count copied into the output queue.
// The count rides beside the enum because the writer must track the
// accepted prefix of a chunk, which a bare verdict cannot express.
struct body_push_result {
    body_push kind = body_push::full;
    std::size_t copied = 0;   // bytes accepted this push (push_end: 0)
};

// Engine-facing seam of one streaming response body. Implemented once
// per protocol engine (the HTTP/1 engine arrives with the
// request-handling tasks); fakes implement it in tests. The engine
// owns the bounded output queue drained towards its transport; the
// writer pushes bounded chunks into it.
class body_sink {
 public:
    virtual ~body_sink() = default;

    // Copies at most from.size() bytes into the output queue and
    // returns the verdict plus the count copied. Room appears only
    // while the engine drains its queue (never as peer
    // acknowledgement). Zero-byte accepted pushes are forbidden; the
    // writer re-pushes them.
    virtual body_push_result push(std::span<const std::byte> from) = 0;

    // Ends the body; `trailers` ride the final framing. The end marker
    // occupies one queue slot, so a full queue reports full. Exactly
    // once per sink; the writer guarantees the single call.
    virtual body_push_result push_end(const http::fields& trailers) = 0;

    // Failure diagnostic; meaningful once a push returned failed.
    virtual const http::outcome& failure() const noexcept = 0;

    // Parks `wait` until the FIRST of: queue room freed, failure
    // raised, or the exchange's stop requested (cancelled). Completes
    // at most once, posted to the waiter's executor (never inline on
    // the completing thread). Precondition: at most one waiter parked
    // per sink.
    virtual void park(body_write_wait& wait) = 0;

    // Detaches a still-parked waiter (the waiter was destroyed without
    // waking). After unpark, the sink must forget the waiter.
    virtual void unpark(body_write_wait& wait) = 0;
};

// Shared state of one parked response write. claim() resolves the wait
// exactly once: a second completion, or a completion racing the
// waiter's destruction, loses the CAS and is a no-op.
struct write_wait_node {
    std::atomic<bool> delivered{false};
    body_room result = body_room::cancelled;
    std::coroutine_handle<> awaiting;
    std::shared_ptr<frame_witness> witness;  // null for foreign coroutines
    executor* target = nullptr;              // null resumes witness-guarded

    bool claim() noexcept {
        bool expected = false;
        return delivered.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire);
    }

    // Posts the resumption to the waiter's executor. Only when no
    // executor is known does the resumption run inline, still guarded
    // by the frame witness.
    void resume_posted() noexcept {
        if (target != nullptr) {
            try {
                target->post([witness = witness, frame = awaiting] {
                    if (witness) {
                        guarded_resume(witness, frame);
                    } else {
                        frame.resume();
                    }
                });
            } catch (...) {
            }
            return;
        }
        if (witness) {
            guarded_resume(witness, awaiting);
        } else {
            awaiting.resume();
        }
    }
};

// One parked response write (frame-resident awaitable, shaped after
// detail::body_wait). The writer co_awaits it; the seam completes it
// at most once. The destructor detaches a still-parked waiter, so
// destroying the writer's frame (task abandoned) can never leave a
// dangling waiter registered with the engine.
class body_write_wait final {
 public:
    body_write_wait(body_sink* sink, stop_token cancel)
        : sink_(sink), node_(std::make_shared<write_wait_node>()),
          cancel_(std::move(cancel)) { }

    body_write_wait(const body_write_wait&) = delete;
    body_write_wait& operator=(const body_write_wait&) = delete;
    body_write_wait(body_write_wait&&) = delete;
    body_write_wait& operator=(body_write_wait&&) = delete;

    body_write_wait& bind_frame(task_frame_base* frame) noexcept {
        frame_ = frame;
        return *this;
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) {
        node_->awaiting = awaiting;
        node_->witness = frame_ ? frame_->frame_witness_ptr() : nullptr;
        node_->target = frame_ ? frame_->frame_executor() : current_executor();
        sink_->park(*this);
        // Seam contract: completion is posted to this executor, never
        // resumed inline on the parking (or completing) thread.
        return std::noop_coroutine();
    }

    body_room await_resume() const noexcept { return node_->result; }

    // Completion entry for body_sink implementations (any thread).
    // Exactly-once via the claim; a losing later completion is a
    // no-op. The posted job owns the node, so it stays safe across
    // waiter destruction.
    void complete(body_room room) noexcept {
        if (!node_->claim()) return;
        node_->result = room;
        node_->resume_posted();
    }

    // The exchange's disconnect token: the seam completes a parked
    // wait as cancelled when it fires.
    const stop_token& cancel_token() const noexcept { return cancel_; }

    ~body_write_wait() {
        if (!node_->delivered.load(std::memory_order_acquire)) {
            sink_->unpark(*this);
        }
    }

 private:
    body_sink* sink_;
    std::shared_ptr<write_wait_node> node_;
    task_frame_base* frame_ = nullptr;
    stop_token cancel_;
};

}  // namespace detail

// The streaming response body of an exchange. Obtained only from
// exchange::writer(); operations are legal only after a successful
// start_response() and fail typed otherwise. One exchange, one writer;
// movable with the exchange, never copied.
//
// Writer state (no locks — writes run on the handler's executor
// thread): started (engine seam attached by start_response), pending
// (the one-outstanding-write rule), finished (push_end accepted; the
// body is over), failed (sticky: an engine failure ends the body),
// closed (writer shut, by finish or exchange abort). A write issued
// after a disconnect fails connection_closed carrying the stored
// disconnect detail; a write interrupted by one returns cancelled.
class response_writer {
 public:
    // Streams one chunk into the engine's bounded output queue,
    // suspending while the queue is full and resuming when room frees.
    // Resolves ok only once the whole chunk was accepted; a partial
    // acceptance surfaces as further internal pushes. An empty chunk
    // fails invalid_argument without touching the engine.
    task<body_write> write(std::span<const std::byte> data) {
        const http::outcome gate = enter(data.empty());
        if (!gate.ok()) {
            co_return body_write{gate, 0};
        }
        pending_ = true;
        std::size_t total = 0;
        while (total < data.size()) {
            const detail::body_push_result pushed =
                sink_->push(data.subspan(total));
            if (pushed.kind == detail::body_push::accepted) {
                // A zero-byte accepted push is engine misuse: count
                // nothing and re-push (never surface progress).
                total += pushed.copied;
                continue;
            }
            if (pushed.kind == detail::body_push::failed) {
                fail_sticky(sink_->failure());
                pending_ = false;
                co_return body_write{failure_, total};
            }
            // Queue at capacity: park until the first of queue room,
            // engine failure, or exchange disconnect. This suspension
            // IS the backpressure (PRD-V3N-REQ-027).
            detail::body_write_wait wait(sink_, cancel_token_);
            if (co_await wait == detail::body_room::cancelled) {
                pending_ = false;
                co_return body_write{
                    http::outcome(http::outcome_code::cancelled,
                                  "response_writer: write cancelled by "
                                  "disconnect"),
                    total};
            }
        }
        pending_ = false;
        co_return body_write{http::outcome::okay(), total};
    }

    // Ends the response body. `trailers` ride the final framing (empty
    // when none). On ok the writer is closed; further write/finish fail
    // invalid_state. Exactly-once: the sink sees push_end at most once
    // (the one-outstanding gate serializes entry; a cancelled finish
    // never half-ends the body).
    task<body_finish> finish(http::fields trailers = http::fields()) {
        const http::outcome gate = enter(false);
        if (!gate.ok()) {
            co_return body_finish{gate};
        }
        pending_ = true;
        for (;;) {
            const detail::body_push_result pushed =
                sink_->push_end(trailers);
            if (pushed.kind == detail::body_push::accepted) {
                finished_ = true;
                pending_ = false;
                close();
                co_return body_finish{http::outcome::okay()};
            }
            if (pushed.kind == detail::body_push::failed) {
                fail_sticky(sink_->failure());
                pending_ = false;
                co_return body_finish{failure_};
            }
            detail::body_write_wait wait(sink_, cancel_token_);
            if (co_await wait == detail::body_room::cancelled) {
                pending_ = false;
                co_return body_finish{
                    http::outcome(http::outcome_code::cancelled,
                                  "response_writer: finish cancelled by "
                                  "disconnect")};
            }
        }
    }

    response_writer(response_writer&&) noexcept = default;
    response_writer& operator=(response_writer&&) noexcept = default;
    response_writer(const response_writer&) = delete;
    response_writer& operator=(const response_writer&) = delete;

 private:
    friend class exchange;

    explicit response_writer(stop_token cancel) noexcept
        : cancel_token_(std::move(cancel)) { }

    // exchange::start_response, after the engine accepted the head. A
    // null sink keeps every write at invalid_state.
    void activate(detail::body_sink* sink) noexcept {
        sink_ = sink;
        started_ = true;
    }

    // exchange::abort, and finish itself on success: the writer shuts
    // down and every later operation fails typed. Idempotent bool, so
    // the body ends exactly once no matter the finish/abort ordering.
    void close() noexcept { closed_ = true; }

    // exchange::disconnect, before the stop fan-out: stores the detail
    // that writes issued after the disconnect report with their typed
    // connection_closed failure.
    void note_disconnect(const http::outcome& reason) noexcept {
        disconnect_reason_ = reason;
    }

    // Shared entry gate of the write operations: fails typed and
    // touches nothing when a push may not start.
    http::outcome enter(bool empty_chunk) const {
        if (pending_) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "response_writer: a write is already "
                                 "outstanding");
        }
        if (cancel_token_.stop_requested()) {
            return http::outcome(http::outcome_code::connection_closed,
                                 disconnect_reason_.message());
        }
        if (closed_) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "response_writer: writer is closed");
        }
        if (!started_ || sink_ == nullptr) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "response_writer: response not started");
        }
        if (empty_chunk) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "response_writer: chunk is empty");
        }
        if (finished_) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "response_writer: body already finished");
        }
        if (failed_) {
            return failure_;
        }
        return http::outcome::okay();
    }

    // Sticky failure: the body is unusable after this point; later
    // writes return the same outcome without touching the engine.
    void fail_sticky(const http::outcome& reason) noexcept {
        failed_ = true;
        failure_ = reason;
    }

    detail::body_sink* sink_ = nullptr;
    stop_token cancel_token_;
    http::outcome failure_;
    http::outcome disconnect_reason_;
    bool started_ = false;
    bool pending_ = false;
    bool finished_ = false;
    bool failed_ = false;
    bool closed_ = false;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_RESPONSE_WRITER_HPP_
