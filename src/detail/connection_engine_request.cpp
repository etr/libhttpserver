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

#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/http1_body_mode.hpp>
#include <httpserver/detail/http1_response_mode.hpp>
#include <httpserver/detail/request_lifecycle.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

// -- route side -------------------------------------------------------------

task<bool> connection_engine::wait_for_head(
    std::shared_ptr<connection_engine> self) {
    for (;;) {
        wake_operation op(self->owner_, self->id_);
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            // The close mark ends the head wait even with a parked
            // complete head: a quiescing (or closing) engine drops
            // pipelined-unstarted heads instead of serving them
            // (TASK-110).
            if (self->shutdown_ || self->close_after_drain_) {
                co_return false;
            }
            if (self->parser_.state() == http1_head_state::complete) {
                co_return true;
            }
            if (self->parser_.state() == http1_head_state::failed) {
                co_return false;
            }
            if (self->eof_) co_return false;
            // Park until a reader absorb nudges the loops (or the
            // transport is released, which terminal-fails the wake).
            // The op registers under mu_ -- the lost-wake closure
            // (TASK-109): every mu_-held state change either fires
            // this op with its wake or is visible at the re-check.
            // Spurious wakes re-check by design.
            op.submit(self->backend_);
        }
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
    // TASK-118: the engine's head-acceptance verdict, shared by the
    // engine sink (writer) and the lifecycle interceptor (reader) --
    // both live in this frame.
    bool head_refused = false;
    http1_exchange_sink engine_sink(self, self->outbox_, head,
                                    &head_refused);
    // TASK-118: the lifecycle interceptor wraps the engine sink, so
    // every committed head (handler, synthesized, or hook-supplied)
    // crosses the after_handler/response_sent firing point (plan D5).
    // The shared head_refused flag publishes the engine's acceptance
    // verdict: a refused head never fires response_sent and fails
    // request_completed.
    lifecycle_sink interceptor(engine_sink, self->hooks_, head,
                               &head_refused);
    wake_body_sink forwarding(*self);
    exchange routed(head, &interceptor, self->id_,
                    body_present ? self->body_.get() : nullptr, &forwarding,
                    self->peer_);
    http1_response_sink& slot = self->outbox_.open(sequence,
                                                   routed.cancellation());
    engine_sink.bind(slot);
    forwarding.bind(slot);
    // The exchange's counted unit spans the live window -- current_
    // binding through finish and the rejection-drain await -- so work
    // the drain counts is literally the calling handler (TASK-110).
    drain_scope::unit exchange_unit{self->scope_};
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        self->current_ = &routed;
    }
    // The watchdog planned against a head-awaiting (or defer) posture;
    // a routed exchange changes the candidate set (a suspended handler
    // arms the suspension deadline), and the bodyless path stages no
    // decoder that would re-arm it (TASK-118: the deferral tick must
    // start at exchange start or a suspended bodyless request parks
    // behind a stale header timer).
    self->rearm_watchdog();
    co_await dispatch_request(self->routes_, self->hooks_,
                              *self->config_.pages, interceptor, routed,
                              self->peers_);
    // TASK-119: a peer-refused settle is a never-responded exchange --
    // nothing will ever queue to its outbox slot, so the writer's
    // drain must not wait for an end marker that cannot arrive. Fail
    // the slots (already-buffered bytes, if any, still flush first);
    // the settle below then marks the close.
    if (routed.disconnected()
            && routed.disconnect_reason().code()
                == http::outcome_code::peer_refused) {
        self->outbox_.abandon();
    }
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        self->current_ = nullptr;
    }
    if (!self->finish_exchange(forwarding, routed, engine_sink, mode)) {
        co_return false;
    }
    // A rejected length-framed body drains to its counted remainder
    // before the next head may parse the stream (TASK-109).
    co_return co_await await_drain(self);
}

task<bool> connection_engine::await_drain(
    std::shared_ptr<connection_engine> self) {
    for (;;) {
        wake_operation op(self->owner_, self->id_);
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            if (self->gate_ != body_gate::draining) co_return true;
            if (self->eof_ || self->shutdown_ || self->close_after_drain_) {
                co_return false;
            }
            // Registers under mu_ (the lost-wake closure, TASK-109):
            // the drain's completion and every bail-out condition
            // change under the same mutex.
            op.submit(self->backend_);
        }
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

bool connection_engine::body_undrained_locked(
        const http1_body_mode& mode) const {
    return mode.kind != http1_body_kind::none
        && (gate_ == body_gate::pending
            || (gate_ == body_gate::admitted
                && (body_ == nullptr || !body_->message_complete())));
}

void connection_engine::settle_exchange_state(bool& keep,
                                              const http1_body_mode& mode) {
    std::lock_guard<std::mutex> lock(mu_);
    // A quiescing engine closes after the in-flight exchange whatever
    // the response said about persistence: the next head (parked or
    // still on the wire) is dropped, and no rejection drain may hold
    // the connection for a remainder it will never serve (TASK-110).
    if (quiescing_) keep = false;
    if (body_undrained_locked(mode)) decide_drain_locked(keep, mode);
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
        // Admitted: length_remaining() counts the octets the decoder
        // framed (staged-but-unread octets die with the decoder); the
        // refused tail parked in pending_tail_ also already arrived, so
        // only the difference is still due on the wire -- never the
        // sum. A tail parked past the counted body is the next
        // pipelined head: leave it for the settle recycle.
        remaining = body_->length_remaining();
        const std::uint64_t parked = pending_tail_.size();
        const std::uint64_t parked_body = parked < remaining ? parked
                                                             : remaining;
        pending_tail_.erase(0, static_cast<std::size_t>(parked_body));
        remaining -= parked_body;
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
    const http::request_head& head, bool* refused_flag)
    : engine_(std::move(engine)), outbox_(outbox), head_(head) {
    refused_flag_ = refused_flag;
}

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
    static_cast<void>(policy);  // delivery knobs arrive with M9 bounded
                                // admission (TASK-116); the sync route
                                // adapter (TASK-111) already carries the
                                // declared cap here
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
    engine_->wake_loops_ordered();
}

void http1_exchange_sink::on_respond(const http::status& s,
                                     const http::fields& f) {
    if (slot_ == nullptr || responded_) return;
    const http::outcome started = slot_->start(head_, s, f,
                                               engine_->config_.clock);
    if (!started.ok()) {
        // The framer refused the head: nothing is buffered; close.
        // The refusal is published first so the lifecycle interceptor
        // (firing response_sent after this returns) can observe it;
        // the failed slot is terminal for the writer's drain, so the
        // close release it owns completes.
        if (refused_flag_ != nullptr) *refused_flag_ = true;
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
    // Ordered against the writer's park check: the committed head is
    // outbox state a parked writer must not miss (TASK-109).
    engine_->wake_loops_ordered();
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
        // Ordered against the writer's park check (TASK-109): the
        // parked writer either fires its registered op here or sees
        // the accepted bytes at its own re-check.
        engine_.wake_loops_ordered();
        engine_.rearm_watchdog();
    }
    return pushed;
}

body_push_result wake_body_sink::push_end(const http::fields& trailers) {
    const body_push_result pushed = inner_->push_end(trailers);
    if (pushed.kind == body_push::accepted) {
        ended_ = true;
        engine_.wake_loops_ordered();
        engine_.rearm_watchdog();
    }
    return pushed;
}

}  // namespace detail

}  // namespace httpserver
