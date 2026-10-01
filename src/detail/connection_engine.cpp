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

// Connection-engine lifecycle, coordination, and the transport loops
// (reader, writer). The route loop and the exchange sinks live in
// connection_engine_request.cpp. See connection_engine.hpp for the
// design contract.

#include <httpserver/detail/connection_engine.hpp>

#include <array>
#include <memory>
#include <string>
#include <utility>

#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

connection_engine::connection_engine(io_poll_backend& backend, worker_pool& pool,
                                     const server::route_registry& routes,
                                     const server::resource_budget& budget,
                                     connection_engine_config config,
                                     std::uint64_t id,
                                     stopped_callback on_stopped)
    : backend_(backend),
      pool_(pool),
      routes_(routes),
      owner_(pool),
      budget_(budget),
      config_(std::move(config)),
      id_(id),
      on_stopped_(std::move(on_stopped)),
      parser_(config_.head),
      outbox_(config_.outbox) {
}

void connection_engine::start() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (started_) return;
        started_ = true;
        live_loops_ = 4;
    }
    if (!budget_.reserve(server::resource::connections, 1, seat_).ok()) {
        // Budget exhaustion: no response (the accept path closes the
        // transport); report the stop so the record is erased.
        backend_.release_connection(id_);
        stopped_callback done;
        {
            std::lock_guard<std::mutex> lock(mu_);
            live_loops_ = 0;
            done = std::move(on_stopped_);
        }
        if (done) done();
        return;
    }
    std::shared_ptr<connection_engine> self = shared_from_this();
    spawn(pool_, reader_loop(self),
          [self](task_result<void>) { self->loop_finished(); });
    spawn(pool_, writer_loop(self),
          [self](task_result<void>) { self->loop_finished(); });
    spawn(pool_, route_loop(self),
          [self](task_result<void>) { self->loop_finished(); });
    spawn(pool_, watchdog_loop(self),
          [self](task_result<void>) { self->loop_finished(); });
}

void connection_engine::shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (shutdown_) return;
        shutdown_ = true;
    }
    disconnect_current(http::outcome_code::connection_closed,
                       "http1 connection engine: server stop");
    outbox_.abandon();
    backend_.release_connection(id_);
    wake_loops();
}

bool connection_engine::running() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return live_loops_ > 0;
}

void connection_engine::wake_loops() noexcept {
    static_cast<void>(backend_.wake());
}

void connection_engine::disconnect_current(http::outcome_code reason,
                                           std::string detail) noexcept {
    http1_body_source* body = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (current_ == nullptr) return;
        static_cast<void>(current_->disconnect(reason, std::move(detail)));
        if (body_active_) body = body_.get();
    }
    // Second half of the body wake pairing (http1_body_source): the
    // exchange's stop is sticky now, so a read parked before this
    // moment wakes here and one parking later fails at its own gate.
    if (body != nullptr) body->cancel_parked();
}

void connection_engine::request_close() noexcept {
    {
        std::lock_guard<std::mutex> lock(mu_);
        close_after_drain_ = true;
    }
    wake_loops();
}

// -- reader side ------------------------------------------------------------

void connection_engine::absorb(std::string_view data) {
    bool body_failed = false;
    note_transport_activity();
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (shutdown_) return;
        if (body_active_ && body_ != nullptr
                && !body_->message_complete()) {
            // Body phase: the decoder keeps the unconsumed tail (its
            // backpressure semantic); the reader re-feeds it ahead of
            // every later chunk.
            std::string combined;
            combined.reserve(pending_tail_.size() + data.size());
            combined.append(pending_tail_).append(data);
            pending_tail_.clear();
            const std::size_t consumed = body_->feed(combined);
            pending_tail_.assign(combined, consumed,
                                 combined.size() - consumed);
            body_failed = body_->failed();
        } else {
            absorb_head_locked(data);
        }
    }
    if (body_failed) {
        // A framing failure poisons the byte stream: unwind the live
        // exchange and close per the decoder's posture.
        disconnect_current(http::outcome_code::protocol_error,
                           "http1 connection engine: request body framing"
                           " failed");
        request_close();
    }
}

void connection_engine::emit_error(std::uint16_t code) {
    stop_source cancel;
    const std::uint64_t sequence = [this] {
        std::lock_guard<std::mutex> lock(mu_);
        return next_sequence_++;
    }();
    http1_response_sink& slot = outbox_.open(sequence, cancel.get_token());
    http::request_head request;
    request.request_protocol = http::protocol::http_1_1;
    request.request_method = http::method::known(http::method_id::get);
    request.raw_target = "/";
    request.route_path = "/";
    request.head_fields.append("Connection", "close");
    http::fields fields;
    fields.append("Content-Length", "0");
    static_cast<void>(slot.start(request, http::status::from_code(code),
                                 fields, config_.clock));
    static_cast<void>(slot.push_end(http::fields()));
    wake_loops();
    request_close();
}

std::uint16_t connection_engine::error_code_for(
    http::outcome_code code) noexcept {
    if (code == http::outcome_code::limit_exceeded) return 431;
    if (code == http::outcome_code::not_supported) return 501;
    return 400;
}

bool connection_engine::stage_body(const http1_body_mode& mode,
                                   std::string seed) {
    bool seed_failed = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        body_ = std::make_unique<http1_body_source>(mode, config_.body);
        body_active_ = true;
        if (!seed.empty()) {
            const std::size_t consumed = body_->feed(seed);
            pending_tail_.assign(seed, consumed, seed.size() - consumed);
            seed_failed = body_->failed();
        }
    }
    // The body_idle deadline can be nearer than whatever is armed.
    rearm_watchdog();
    return seed_failed;
}

void connection_engine::absorb_head_locked(std::string_view data) {
    // A complete untaken head accepts nothing: park the bytes for the
    // route loop (they are the body seed or the next head).
    if (parser_.state() == http1_head_state::complete) {
        pending_tail_.append(data);
        return;
    }
    if (!pending_tail_.empty()) {
        std::string queued = std::move(pending_tail_);
        pending_tail_.clear();
        parser_.feed(queued);
        if (parser_.state() == http1_head_state::complete) {
            pending_tail_.append(data);
            return;
        }
    }
    parser_.feed(data);
}

task<void> connection_engine::reader_loop(
    std::shared_ptr<connection_engine> self) {
    std::array<std::byte, k_read_buffer_bytes> buffer;
    for (;;) {
        bool stopping = false;
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            stopping = self->shutdown_ || self->close_after_drain_;
        }
        if (stopping) co_return;
        read_operation op(self->owner_, self->id_,
                          std::span<std::byte>(buffer.data(), buffer.size()));
        op.submit(self->backend_);
        const io_result r = co_await std::move(op);
        if (r.code == http::outcome_code::ok) {
            self->absorb(std::string_view(
                reinterpret_cast<const char*>(buffer.data()), r.transferred));
            self->wake_loops();
            continue;
        }
        if (r.code == http::outcome_code::connection_closed) {
            // Peer hangup: the route loop closes cleanly between
            // requests; a mid-exchange hangup disconnects the live
            // exchange so a parked body read unwinds.
            bool routing = false;
            {
                std::lock_guard<std::mutex> lock(self->mu_);
                self->eof_ = true;
                routing = self->current_ != nullptr;
            }
            if (routing) {
                self->disconnect_current(
                    http::outcome_code::connection_closed,
                    "http1 connection engine: peer hangup mid-exchange");
            }
        } else {
            self->disconnect_current(http::outcome_code::connection_closed,
                                     "http1 connection engine: transport"
                                     " read failed");
            self->request_close();
        }
        self->wake_loops();
        co_return;
    }
}

// -- writer side ------------------------------------------------------------

task<void> connection_engine::writer_loop(
    std::shared_ptr<connection_engine> self) {
    // The engine drives the socket writes itself (copy_front /
    // write_operation / consume_front) so every completed write is
    // visible as transport activity for the write-idle deadline.
    std::array<std::byte, k_write_buffer_bytes> buffer;
    for (;;) {
        const std::size_t buffered = self->outbox_.copy_front(buffer);
        if (buffered == 0) {
            bool drained = false;
            bool done = false;
            {
                std::lock_guard<std::mutex> lock(self->mu_);
                drained = self->outbox_.empty();
                done = self->route_done_ || self->shutdown_;
            }
            // Exits only once the route loop is finished AND the outbox is
            // drained, so a close-after-response never truncates a body.
            // The writer owns the release on the close paths: every other
            // loop is parked in a transport operation only a release can
            // complete (a parked read on an idle keep-alive peer, for one).
            if (drained && done) {
                std::lock_guard<std::mutex> lock(self->mu_);
                if (self->close_after_drain_ || self->shutdown_) {
                    self->backend_.release_connection(self->id_);
                }
                co_return;
            }
            // Nothing to flush yet, or the front waits on the handler:
            // park. Wake ops coalesce globally -- a spurious completion
            // re-checks; a terminal one means the transport is gone.
            wake_operation op(self->owner_, self->id_);
            op.submit(self->backend_);
            const io_result r = co_await std::move(op);
            if (r.code != http::outcome_code::ok) co_return;
            continue;
        }
        write_operation op(self->owner_, self->id_,
                           std::span<const std::byte>(buffer.data(),
                                                      buffered));
        op.submit(self->backend_);
        const io_result r = co_await std::move(op);
        if (r.code != http::outcome_code::ok) co_return;
        self->outbox_.consume_front(r.transferred);
        self->note_transport_activity();
    }
}

// -- coordination -----------------------------------------------------------

// -- watchdog ---------------------------------------------------------------

connection_engine::watchdog_plan connection_engine::plan_watchdog_locked() {
    watchdog_plan plan;
    if (shutdown_ || close_after_drain_) {
        plan.exit = true;
        return plan;
    }
    // Nearest-deadline order: a decoding body, then queued bytes with a
    // stalled peer, then a suspended exchange. While an exchange is
    // routed and none of those holds, no inventory deadline applies --
    // the plan defers (TASK-110's drain semantics bound it instead).
    if (body_active_ && body_ != nullptr
            && !body_->message_complete()) {
        plan.deadline = last_activity_ + config_.timeouts.body_idle;
        return plan;
    }
    if (outbox_.queued_bytes() > 0) {
        plan.deadline = last_activity_ + config_.timeouts.write_idle;
        return plan;
    }
    if (current_ != nullptr) {
        if (!current_->suspended()) {
            suspension_anchor_.reset();
            plan.defer = true;
            return plan;
        }
        // Anchored at first sight: recomputing now + suspension on
        // every check would slide the deadline forever.
        if (!suspension_anchor_.has_value()) {
            suspension_anchor_ = std::chrono::steady_clock::now();
        }
        plan.deadline = *suspension_anchor_ + config_.timeouts.suspension;
        return plan;
    }
    suspension_anchor_.reset();
    plan.deadline = last_activity_ + config_.timeouts.header;
    return plan;
}

bool connection_engine::watchdog_due(
    std::chrono::steady_clock::time_point deadline) {
    std::lock_guard<std::mutex> lock(mu_);
    const watchdog_plan plan = plan_watchdog_locked();
    return !plan.exit && !plan.defer && plan.deadline == deadline
        && std::chrono::steady_clock::now() >= deadline;
}

void connection_engine::rearm_watchdog() noexcept {
    std::shared_ptr<op_state> pending;
    {
        std::lock_guard<std::mutex> lock(mu_);
        pending = pending_timer_;
    }
    if (pending != nullptr) {
        static_cast<void>(backend_.request_cancel(*pending));
    }
}

void connection_engine::note_transport_activity() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        last_activity_ = std::chrono::steady_clock::now();
    }
    rearm_watchdog();
}

void connection_engine::enforce_timeout() noexcept {
    // A fired deadline means the peer or the exchange will not
    // progress. Disconnect the live exchange (its parked handler
    // unwinds through run_route), mark the close, and release the
    // transport so every parked loop terminal-completes. No response
    // is synthesized: a timed-out peer is not reading one.
    disconnect_current(http::outcome_code::timeout,
                       "http1 connection engine: watchdog timeout");
    {
        std::lock_guard<std::mutex> lock(mu_);
        close_after_drain_ = true;
    }
    backend_.release_connection(id_);
    wake_loops();
}

task<void> connection_engine::watchdog_loop(
    std::shared_ptr<connection_engine> self) {
    for (;;) {
        watchdog_plan plan;
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            plan = self->plan_watchdog_locked();
        }
        if (plan.exit) co_return;
        const auto deadline = plan.defer
            ? std::chrono::steady_clock::now() + k_watchdog_tick
            : plan.deadline;
        timer_operation op(self->owner_, self->id_, deadline);
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            self->pending_timer_ = op.state();
        }
        op.submit(self->backend_);
        const io_result r = co_await std::move(op);
        {
            std::lock_guard<std::mutex> lock(self->mu_);
            self->pending_timer_.reset();
        }
        // A released transport ends the watchdog with the connection.
        if (r.code == http::outcome_code::connection_closed) co_return;
        // A cancellation (state moved) and a deferred re-check tick
        // both re-plan with fresh eyes.
        if (r.code != http::outcome_code::ok || plan.defer) continue;
        // Fired: enforce only when the deadline still governs the
        // current state (activity moved it -- re-arm).
        if (!self->watchdog_due(deadline)) continue;
        self->enforce_timeout();
        co_return;
    }
}

void connection_engine::loop_finished() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (live_loops_ > 0) --live_loops_;
        if (live_loops_ > 0) return;
    }
    finalize();
}

void connection_engine::finalize() {
    backend_.release_connection(id_);
    stopped_callback done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        done = std::move(on_stopped_);
    }
    if (done) done();
}

}  // namespace detail

}  // namespace httpserver
