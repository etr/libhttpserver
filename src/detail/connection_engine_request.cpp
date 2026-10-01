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

// Connection-engine request path: the head wait, one sequential
// exchange through run_route, the exchange tail (end synthesis,
// keep-alive verdict, pipelined-byte recycle), and the two engine
// sinks. Lifecycle and the transport loops live in
// connection_engine.cpp.

#include <httpserver/detail/connection_engine.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/http1_body_mode.hpp>
#include <httpserver/detail/http1_response_mode.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

// -- route side -------------------------------------------------------------

task<bool> connection_engine::wait_for_head(
    std::shared_ptr<connection_engine> self) {
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            if (self->shutdown_) co_return false;
            if (self->parser_.state() == http1_head_state::complete) {
                co_return true;
            }
            if (self->parser_.state() == http1_head_state::failed) {
                co_return false;
            }
            if (self->eof_) co_return false;
        }
        // Park until a reader absorb nudges the loops (or the transport
        // is released, which terminal-fails the wake). Spurious wakes
        // re-check by design: wake ops coalesce globally.
        wake_operation op(self->owner_, self->id_);
        op.submit(self->backend_);
        const io_result r = co_await std::move(op);
        if (r.code != http::outcome_code::ok) co_return false;
    }
}

task<bool> connection_engine::serve_one(
    std::shared_ptr<connection_engine> self) {
    // Seed: the parser's buffered residue plus whatever the reader
    // parked behind the complete head. Both belong to this exchange's
    // body (or, with no framing, to the next head).
    http::request_head head;
    std::string seed;
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        seed.assign(self->parser_.residue_view());
        self->parser_.consume_residue(self->parser_.residue_size());
        head = self->parser_.take();
        seed.append(self->pending_tail_);
        self->pending_tail_.clear();
    }
    const http1_body_mode mode = http1_body_mode::compute(head);
    if (mode.kind == http1_body_kind::rejected) {
        self->emit_error(
            self->error_code_for(mode.failure.code()));
        co_return false;
    }
    const bool body_present = mode.kind != http1_body_kind::none;
    if (body_present) {
        // The decoder exists now (the exchange ctor needs its pointer)
        // but stays unfed: the seed parks as this exchange's early
        // bytes until the handler admits the body.
        self->stage_body(mode, std::move(seed));
    } else {
        // No framing: every seed byte belongs to the next pipelined
        // head. Park it where the settle recycle and the head-phase
        // absorb both look, and nudge a reader gated on the tail cap.
        std::lock_guard<std::mutex> lock(self->mu_);
        self->pending_tail_.append(seed);
        self->wake_loops();
    }
    const std::uint64_t sequence = [&self] {
        std::lock_guard<std::mutex> lock(self->mu_);
        return self->next_sequence_++;
    }();
    http1_exchange_sink engine_sink(self, self->outbox_, head);
    wake_body_sink forwarding(*self);
    exchange routed(head, &engine_sink, self->id_,
                    body_present ? self->body_.get() : nullptr, &forwarding);
    http1_response_sink& slot = self->outbox_.open(sequence,
                                                   routed.cancellation());
    engine_sink.bind(slot);
    forwarding.bind(slot);
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        self->current_ = &routed;
    }
    co_await run_route(self->routes_, routed);
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        self->current_ = nullptr;
    }
    const bool keep = self->finish_exchange(forwarding, routed, engine_sink,
                                            mode);
    if (!keep) co_return false;
    // A rejected length-framed body drains to its counted remainder
    // before the next head may parse the stream (TASK-109): park until
    // the drain completes. EOF, a close verdict, or a stop ends the
    // connection instead of waiting out the remainder.
    for (;;) {
        bool drained = false;
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            drained = self->gate_ != body_gate::draining;
            if (!drained && (self->eof_ || self->shutdown_
                             || self->close_after_drain_)) {
                co_return false;
            }
        }
        if (drained) co_return true;
        wake_operation op(self->owner_, self->id_);
        op.submit(self->backend_);
        const io_result r = co_await std::move(op);
        if (r.code != http::outcome_code::ok) co_return false;
    }
}

bool connection_engine::finish_exchange(wake_body_sink& forwarding,
                                        const exchange& routed,
                                        http1_exchange_sink& engine_sink,
                                        const http1_body_mode& mode) {
    // Head-only responses (respond() without a streaming body) still
    // need their end marker so the framing closes validly.
    if (!forwarding.ended() && !routed.disconnected()) {
        static_cast<void>(forwarding.push_end(http::fields()));
    }
    bool keep = !routed.disconnected() && !engine_sink.upgraded()
        && engine_sink.responded_ok()
        && engine_sink.keepalive() == http1_keepalive::keep_alive;
    settle_exchange_state(keep, mode);
    wake_loops();
    // Back to awaiting a head (or closing, or a fresh drain): the next
    // deadline can be nearer than the one armed for the exchange.
    rearm_watchdog();
    return keep;
}

void connection_engine::settle_exchange_state(bool& keep,
                                              const http1_body_mode& mode) {
    std::lock_guard<std::mutex> lock(mu_);
    // Undrained: the exchange ends with body octets nobody consumed --
    // the decoder unfed (never admitted) or fed but incomplete.
    const bool undrained = mode.kind != http1_body_kind::none
        && (gate_ == body_gate::pending
            || (gate_ == body_gate::admitted
                && (body_ == nullptr || !body_->message_complete())));
    if (undrained) decide_drain_locked(keep, mode);
    // A kept verdict implies the body (if any) is accounted for, so
    // parked pipelined bytes are exactly the next head: recycle them
    // into the parser. The per-exchange gate closes unless a drain
    // holds it; the next serve_one reopens it when its framing says so.
    if (keep && !pending_tail_.empty()) {
        std::string back = std::move(pending_tail_);
        pending_tail_.clear();
        parser_.feed(back);
    }
    if (gate_ != body_gate::draining) gate_ = body_gate::none;
    std::string().swap(early_bytes_);
    if (!keep) close_after_drain_ = true;
}

void connection_engine::decide_drain_locked(bool& keep,
                                            const http1_body_mode& mode) {
    if (!keep || eof_ || mode.kind != http1_body_kind::length) {
        keep = false;
        return;
    }
    std::uint64_t remaining = mode.content_length;
    if (gate_ == body_gate::pending) {
        const std::uint64_t early = early_bytes_.size();
        if (early > remaining) {
            // Early bytes past the counted body are the next pipelined
            // head: park them where the settle recycle looks.
            pending_tail_.append(
                early_bytes_, static_cast<std::size_t>(remaining),
                static_cast<std::size_t>(early - remaining));
        }
        std::string().swap(early_bytes_);
        remaining = early < remaining ? remaining - early : 0;
    } else {
        // Admitted: never-fed octets plus the decoder's refused tail
        // (staged-but-unread octets die with the decoder).
        remaining = body_->length_remaining() + pending_tail_.size();
        pending_tail_.clear();
    }
    if (remaining == 0) return;   // the whole body already arrived
    gate_ = body_gate::draining;
    drain_remaining_ = remaining;
    drain_anchor_ = std::chrono::steady_clock::now();
}

task<void> connection_engine::route_loop(
    std::shared_ptr<connection_engine> self) {
    // Sequential exchanges per connection (the deliberate TASK-108
    // posture); handlers across connections interleave on the pool.
    // Pipelined heads buffer in the parser and the outbox ordering, so
    // order is preserved without re-entry.
    for (;;) {
        if (!(co_await wait_for_head(self))) {
            // A failed head answers bare (400/431/501) and closes; EOF
            // and shutdown just close.
            http::outcome failure;
            {
                std::lock_guard<std::mutex> lock(self->mu_);
                if (self->parser_.state() == http1_head_state::failed) {
                    failure = self->parser_.failure();
                }
            }
            if (failure.ok()) {
                self->request_close();
            } else {
                self->emit_error(self->error_code_for(failure.code()));
            }
            break;
        }
        if (!(co_await serve_one(self))) break;
    }
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        self->route_done_ = true;
    }
    self->wake_loops();
}

// -- engine sinks -----------------------------------------------------------

http1_exchange_sink::http1_exchange_sink(
    std::shared_ptr<connection_engine> engine, http1_response_outbox& outbox,
    const http::request_head& head)
    : engine_(std::move(engine)), outbox_(outbox), head_(head) { }

void http1_exchange_sink::bind(http1_response_sink& slot) noexcept {
    slot_ = &slot;
}

bool http1_exchange_sink::responded_ok() const noexcept {
    return responded_;
}

http1_keepalive http1_exchange_sink::keepalive() const noexcept {
    return keepalive_;
}

bool http1_exchange_sink::upgraded() const noexcept {
    return upgraded_;
}

void http1_exchange_sink::on_admit(const body_policy& policy) {
    static_cast<void>(policy);  // delivery knobs arrive with TASK-110
    // The admission transition first (TASK-109): the parked early
    // bytes seed the decoder and the gated reader resumes; a seed that
    // already fails framing disconnects the exchange and never earns
    // the interim.
    if (!engine_->admit_early_body()) return;
    // Expect: 100-continue (RFC 9110 section 10.1.1): the interim rides
    // ahead of the final head in the same slot. Admission-conditional
    // only: it fires even when the whole body already arrived (the RFC
    // allows omission; the simple rule). Unknown Expect values stay
    // ignored -- no 417 posture.
    const std::optional<std::string_view> expect =
        head_.head_fields.first("expect");
    if (slot_ == nullptr || expect == std::nullopt
        || head_.request_protocol != http::protocol::http_1_1
        || !detail_head::ascii_iequals(*expect, "100-continue")) {
        return;
    }
    static_cast<void>(slot_->interim(100));
    engine_->wake_loops();
}

void http1_exchange_sink::on_respond(const http::status& s,
                                     const http::fields& f) {
    if (slot_ == nullptr || responded_) return;
    const http::outcome started = slot_->start(head_, s, f,
                                               engine_->config_.clock);
    if (!started.ok()) {
        // The framer refused the head: nothing is buffered; close.
        keepalive_ = http1_keepalive::close;
        engine_->request_close();
        return;
    }
    responded_ = true;
    // The verdict is a pure recomputation over the committed inputs
    // (the framer computes the identical decision for the emission).
    const http1_response_mode mode = http1_response_mode::compute(head_, s, f);
    keepalive_ = http1_response_keepalive(head_, mode.kind,
                                          mode.close_policy);
    engine_->wake_loops();
    // Queued bytes switch the plan to write_idle, which can be nearer
    // than the deadline currently armed.
    engine_->rearm_watchdog();
}

void http1_exchange_sink::on_upgrade(const ws_upgrade_options& options) {
    static_cast<void>(options);
    // The upgrade handshake is out of scope for M8: the clean posture
    // closes the connection once the committed head drains.
    upgraded_ = true;
    keepalive_ = http1_keepalive::close;
    engine_->request_close();
}

void http1_exchange_sink::on_abort() {
    // Post-commit failure: reset instead of a second response.
    outbox_.abandon();
    keepalive_ = http1_keepalive::close;
    engine_->request_close();
}

body_push_result wake_body_sink::push(std::span<const std::byte> from) {
    const body_push_result pushed = inner_->push(from);
    if (pushed.kind == body_push::accepted) {
        engine_.wake_loops();
        engine_.rearm_watchdog();
    }
    return pushed;
}

body_push_result wake_body_sink::push_end(const http::fields& trailers) {
    const body_push_result pushed = inner_->push_end(trailers);
    if (pushed.kind == body_push::accepted) {
        ended_ = true;
        engine_.wake_loops();
        engine_.rearm_watchdog();
    }
    return pushed;
}

}  // namespace detail

}  // namespace httpserver
