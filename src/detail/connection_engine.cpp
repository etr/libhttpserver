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
        live_loops_ = 3;
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
    std::lock_guard<std::mutex> lock(mu_);
    if (current_ != nullptr) {
        static_cast<void>(current_->disconnect(reason, std::move(detail)));
    }
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
            // Peer hangup: the route loop observes the flag and closes
            // cleanly; a mid-exchange disconnect happens there too.
            std::lock_guard<std::mutex> lock(self->mu_);
            self->eof_ = true;
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
    for (;;) {
        const io_result r = co_await self->outbox_.write_front(
            self->backend_, self->owner_, self->id_);
        if (r.code != http::outcome_code::ok) co_return;
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
        if (drained) {
            // Nothing buffered and more may come: park. Wake ops
            // coalesce globally -- a spurious completion re-checks.
            wake_operation op(self->owner_, self->id_);
            op.submit(self->backend_);
            static_cast<void>(co_await std::move(op));
        }
    }
}

// -- coordination -----------------------------------------------------------

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
