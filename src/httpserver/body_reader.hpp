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

#ifndef SRC_HTTPSERVER_BODY_READER_HPP_
#define SRC_HTTPSERVER_BODY_READER_HPP_

// Bounded request-body delivery for the v3 exchange (architecture
// §3.1, DR-V3-003, PRD-V3N-REQ-021/022/025). A handler that admitted a
// body reads it through exchange::body(), a body_reader:
//
//   - read_some(destination) performs one bounded incremental read;
//     exactly one read (or collect) may be outstanding at a time;
//   - collect(maximum) buffers the whole body and fails limit_exceeded
//     exactly when the body exceeds the cap (exactly-at-cap succeeds);
//   - a read parked with nothing staged wakes on the first of bytes
//     staged, end of body, engine failure, or exchange disconnect.
//
// The engine side of the seam is detail::body_source, declared in this
// public header for the same reason detail::exchange_sink is: the
// reader's inline methods invoke it and a public header may not
// include a private one. It is engine plumbing, not consumer surface.
// The pull model makes consumption and receive credit the same event:
// credit for staged bytes is released only inside pull(), so a handler
// that stops reading applies backpressure by never draining the
// engine's bounded staging queue.
//
// Threading contract: reads run on the handler's executor thread (no
// locks); a parked read's completion may arrive from any thread and is
// posted to the reader's executor, mirroring the resume-signal rules.
// Lifetime: body_read::data views the caller's buffer; moving an
// exchange with an outstanding read is a contract violation (reads
// happen between moves).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <coroutine>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

// Typed result of one bounded incremental body read. On a successful
// data read, `data` views the caller's destination buffer (never
// empty) and stays valid until the buffer is reused or destroyed.
struct body_read {
    http::outcome status;
    std::span<const std::byte> data;
    bool end_of_body = false;
};

// Typed result of collect(maximum). ok carries the whole remaining
// body, size() <= maximum; limit_exceeded carries nothing.
struct body_collect {
    http::outcome status;
    std::vector<std::byte> data;
};

namespace detail {

class body_wait;

// Verdict of one body_source::pull().
enum class body_pull : std::uint8_t {
    data,    // staged bytes copied into the destination
    end,     // authoritative end of body; trailers are final
    empty,   // nothing staged; the reader must park
    failed,  // framing/protocol failure; see body_source::failure()
};

// What a parked body_wait completes with.
enum class body_wake : std::uint8_t {
    data, end, failed,
    cancelled,  // the exchange's stop fired before completion
};

// A pull verdict plus the byte count copied into the destination. The
// count rides beside the enum because the reader must hand the caller
// exactly the consumed prefix, which a bare verdict cannot express.
struct body_pull_result {
    body_pull kind = body_pull::empty;
    std::size_t copied = 0;
};

// Engine-facing seam of one admitted body. Implemented once per
// protocol engine (the HTTP/1 engine arrives with the request-handling
// tasks); fakes implement it in tests. The engine owns the bounded
// staging queue fed by its transport reads; the reader pulls bounded
// chunks out of it.
class body_source {
 public:
    virtual ~body_source() = default;

    // Moves at most into.size() staged bytes into `into` and returns
    // the verdict plus the count copied. Returning data is THE
    // consumption event: implementations release receive credit
    // (backpressure relief) for exactly the bytes copied here — never
    // for bytes merely staged. Zero-byte pulls returning data are
    // forbidden; the reader skips them.
    virtual body_pull_result pull(std::span<std::byte> into) = 0;

    // Received trailers; final once a pull returned end.
    virtual const http::fields& trailers() const noexcept = 0;

    // Failure diagnostic; meaningful once a pull returned failed.
    virtual const http::outcome& failure() const noexcept = 0;

    // Parks `wait` until the FIRST of: bytes staged (data), end
    // reached, failure raised, or the exchange's stop requested
    // (cancelled). Completes at most once, posted to the waiter's
    // executor (never inline on the completing thread). Precondition:
    // at most one waiter parked per source.
    virtual void park(body_wait& wait) = 0;

    // Detaches a still-parked waiter (the waiter was destroyed without
    // waking). After unpark, the source must forget the waiter.
    virtual void unpark(body_wait& wait) = 0;
};

// Shared state of one parked body read. claim() resolves the wait
// exactly once: a second completion, or a completion racing the
// waiter's destruction, loses the CAS and is a no-op.
struct wait_node {
    std::atomic<bool> delivered{false};
    body_wake result = body_wake::cancelled;
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

// One parked body read (frame-resident awaitable, shaped after the
// resume waiter). The reader co_awaits it; the seam completes it at
// most once. The destructor detaches a still-parked waiter, so
// destroying the reader's frame (task abandoned) can never leave a
// dangling waiter registered with the engine.
class body_wait final {
 public:
    body_wait(body_source* source, stop_token cancel)
        : source_(source), node_(std::make_shared<wait_node>()),
          cancel_(std::move(cancel)) { }

    body_wait(const body_wait&) = delete;
    body_wait& operator=(const body_wait&) = delete;
    body_wait(body_wait&&) = delete;
    body_wait& operator=(body_wait&&) = delete;

    body_wait& bind_frame(task_frame_base* frame) noexcept {
        frame_ = frame;
        return *this;
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) {
        node_->awaiting = awaiting;
        node_->witness = frame_ ? frame_->frame_witness_ptr() : nullptr;
        node_->target = frame_ ? frame_->frame_executor() : current_executor();
        source_->park(*this);
        // Seam contract: completion is posted to this executor, never
        // resumed inline on the parking (or completing) thread.
        return std::noop_coroutine();
    }

    body_wake await_resume() const noexcept { return node_->result; }

    // Completion entry for body_source implementations (any thread).
    // Exactly-once via the claim; a losing later completion is a
    // no-op. The posted job owns the node, so it stays safe across
    // waiter destruction.
    void complete(body_wake wake) noexcept {
        if (!node_->claim()) return;
        node_->result = wake;
        node_->resume_posted();
    }

    // The exchange's disconnect token: the seam completes a parked
    // wait as cancelled when it fires.
    const stop_token& cancel_token() const noexcept { return cancel_; }

    ~body_wait() {
        if (!node_->delivered.load(std::memory_order_acquire)) {
            source_->unpark(*this);
        }
    }

 private:
    body_source* source_;
    std::shared_ptr<wait_node> node_;
    task_frame_base* frame_ = nullptr;
    stop_token cancel_;
};

}  // namespace detail

// The admitted request body of an exchange. Obtained only from
// exchange::body(); operations are legal only after a successful
// admit_body() and fail typed otherwise. One exchange, one reader;
// movable with the exchange, never copied.
//
// Reader state (no locks — reads run on the handler's executor
// thread): admitted (engine seam attached), pending (the
// one-outstanding-read rule), at_end (trailers final), failed (sticky:
// over-limit or engine failure ends the body), closed (exchange
// terminal). A read issued after a disconnect fails connection_closed
// carrying the stored disconnect detail; a read interrupted by one
// returns cancelled.
class body_reader {
 public:
    // Bounded incremental read: resolves with at least one byte (and
    // at most destination.size()) unless the body ends first. An empty
    // destination fails invalid_argument without touching the engine.
    task<body_read> read_some(std::span<std::byte> destination) {
        const http::outcome gate = enter(destination.empty());
        if (!gate.ok()) {
            co_return body_read{gate, {}, false};
        }
        pending_ = true;
        for (;;) {
            const detail::body_pull_result pulled = source_->pull(destination);
            if (pulled.kind == detail::body_pull::data) {
                if (pulled.copied == 0) {
                    // Forbidden zero-byte data pull (engine misuse):
                    // never surface an empty data span; re-pull.
                    continue;
                }
                pending_ = false;
                co_return body_read{http::outcome::okay(),
                                    destination.first(pulled.copied), false};
            }
            if (pulled.kind == detail::body_pull::end) {
                at_end_ = true;
                pending_ = false;
                co_return body_read{http::outcome::okay(), {}, true};
            }
            if (pulled.kind == detail::body_pull::failed) {
                fail_sticky(source_->failure());
                pending_ = false;
                co_return body_read{failure_, {}, false};
            }
            // Nothing staged: park until the first of bytes staged,
            // end, failure, or exchange disconnect.
            detail::body_wait wait(source_, cancel_token_);
            if (co_await wait == detail::body_wake::cancelled) {
                pending_ = false;
                co_return body_read{
                    http::outcome(http::outcome_code::cancelled,
                                  "body_reader: read cancelled by disconnect"),
                    {}, false};
            }
        }
    }

    // The received trailers; final once a read returned end_of_body
    // (or a collect succeeded). Empty when none were received.
    const http::fields& trailers() const noexcept {
        if (source_ != nullptr) return source_->trailers();
        static const http::fields none;
        return none;
    }

    // Fully buffers the remaining body, at most `maximum` bytes. Fails
    // limit_exceeded (diagnostic naming the cap) exactly when the body
    // exceeds it; exactly-at-cap succeeds. On success the reader is at
    // end of body and trailers() is final.
    task<body_collect> collect(std::uint64_t maximum) {
        const http::outcome gate = enter(false);
        if (!gate.ok()) {
            co_return body_collect{gate, {}};
        }
        pending_ = true;
        std::vector<std::byte> out;
        std::byte scratch[COLLECT_CHUNK_BYTES];
        for (;;) {
            const std::uint64_t room =
                maximum - static_cast<std::uint64_t>(out.size());
            const detail::body_pull_result pulled = source_->pull(
                std::span<std::byte>(scratch, collect_want(room)));
            if (pulled.kind == detail::body_pull::data && pulled.copied > 0) {
                if (pulled.copied > room) {
                    // Exact outcome: the bytes beyond the cap prove the
                    // over-limit body; the tripping pull is discarded,
                    // never appended.
                    fail_sticky(http::outcome(
                        http::outcome_code::limit_exceeded,
                        "body_reader: body exceeds the collect() cap of "
                        + std::to_string(maximum) + " bytes"));
                    pending_ = false;
                    co_return body_collect{failure_, {}};
                }
                out.insert(out.end(), scratch, scratch + pulled.copied);
                continue;
            }
            if (pulled.kind == detail::body_pull::end) {
                at_end_ = true;
                pending_ = false;
                co_return body_collect{http::outcome::okay(), std::move(out)};
            }
            if (pulled.kind == detail::body_pull::failed) {
                fail_sticky(source_->failure());
                pending_ = false;
                co_return body_collect{failure_, {}};
            }
            // Nothing staged: park until the first of bytes staged,
            // end, failure, or exchange disconnect.
            detail::body_wait wait(source_, cancel_token_);
            if (co_await wait == detail::body_wake::cancelled) {
                pending_ = false;
                co_return body_collect{
                    http::outcome(http::outcome_code::cancelled,
                                  "body_reader: read cancelled by disconnect"),
                    {}};
            }
        }
    }

    body_reader(body_reader&& other) noexcept = default;
    body_reader& operator=(body_reader&& other) noexcept = default;
    body_reader(const body_reader&) = delete;
    body_reader& operator=(const body_reader&) = delete;

 private:
    friend class exchange;

    explicit body_reader(stop_token cancel) noexcept
        : cancel_token_(std::move(cancel)) { }

    // exchange::admit_body, after the engine accepted the admission. A
    // null source keeps every read at invalid_state.
    void activate(detail::body_source* source) noexcept {
        source_ = source;
        admitted_ = true;
    }

    // exchange::respond/upgrade, before committing: the exchange is
    // terminal and the body is over.
    void close() noexcept { closed_ = true; }

    // exchange::disconnect, before the stop fan-out: stores the detail
    // that reads issued after the disconnect report with their typed
    // connection_closed failure.
    void note_disconnect(const http::outcome& reason) noexcept {
        disconnect_reason_ = reason;
    }

    // Shared entry gate of the read operations: fails typed and
    // touches nothing when a pull may not start.
    http::outcome enter(bool empty_destination) const {
        if (pending_) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "body_reader: a read is already outstanding");
        }
        if (cancel_token_.stop_requested()) {
            return http::outcome(http::outcome_code::connection_closed,
                                 disconnect_reason_.message());
        }
        if (closed_) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "body_reader: exchange is terminal");
        }
        if (!admitted_ || source_ == nullptr) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "body_reader: body not admitted");
        }
        if (empty_destination) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "body_reader: destination buffer is empty");
        }
        if (at_end_) {
            return http::outcome(http::outcome_code::invalid_state,
                                 "body_reader: body already at end");
        }
        if (failed_) {
            return failure_;
        }
        return http::outcome::okay();
    }

    // Sticky failure: the body is unusable after this point; later
    // reads return the same outcome without touching the engine.
    void fail_sticky(const http::outcome& reason) noexcept {
        failed_ = true;
        failure_ = reason;
    }

    // Pull size for collect: never more than the remaining cap room,
    // plus one probe byte that proves an over-limit body exactly. The
    // probe may consume one byte of the tripping pull, which is
    // discarded; the outcome stays exact.
    static std::size_t collect_want(std::uint64_t room) noexcept {
        return room < COLLECT_CHUNK_BYTES
            ? static_cast<std::size_t>(room) + 1
            : COLLECT_CHUNK_BYTES;
    }

    static constexpr std::size_t COLLECT_CHUNK_BYTES = 512;

    detail::body_source* source_ = nullptr;
    stop_token cancel_token_;
    http::outcome failure_;
    http::outcome disconnect_reason_;
    bool admitted_ = false;
    bool pending_ = false;
    bool at_end_ = false;
    bool failed_ = false;
    bool closed_ = false;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_BODY_READER_HPP_
