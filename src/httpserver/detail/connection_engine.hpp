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

// Per-connection HTTP/1 engine of the native server (TASK-108,
// architecture §3.4, DR-V3-006). One connection_engine per accepted
// connection; the task tree is connection_run over three cooperating
// loops on the shared worker pool:
//
//   reader_loop  transport -> parser / body decoder (EOF and reset
//                detection; feeds never lose bytes -- a complete
//                untaken head parks its bytes in the pending tail for
//                the route loop to hand to the body path or back to the
//                parser).
//   route_loop   sequential exchanges: wait head -> seed body -> route
//                through run_route -> end synthesis -> keep-alive
//                verdict. Pipelined heads buffer in the parser/outbox
//                ordering, never re-ordered.
//   writer_loop  outbox -> transport (write_front), parking on a wake
//                operation whenever nothing is buffered. wake ops
//                coalesce globally: every parked loop tolerates a
//                spurious wake by re-checking its condition.
//
// Coordination state is mutex-guarded (short critical sections only);
// every loop exits when the connection is released, and the LAST loop
// to finish releases the transport and runs the stopped callback (the
// listener engine erases its record there). shutdown() is the
// server-stop path: disconnect the live exchange (handler-safe per
// DR-V3-008), abandon the outbox, release the connection -- all
// non-blocking, callable from any thread including inside a handler.
//
// Deliberate TASK-108 posture: exchanges on one connection run
// sequentially (the outbox ordering design supports a later concurrent
// refinement); the drain-ticket semantics of stop stay with TASK-110.
#if !defined(HTTPSERVER_COMPILATION)
#error "connection_engine.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_CONNECTION_ENGINE_HPP_
#define SRC_HTTPSERVER_DETAIL_CONNECTION_ENGINE_HPP_

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/http1_body_decoder.hpp>
#include <httpserver/detail/http1_body_source.hpp>
#include <httpserver/detail/http1_error_synth.hpp>
#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/detail/http1_response_outbox.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_operation.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/worker_pool.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace detail {

class connection_engine;
class http1_exchange_sink;
class wake_body_sink;

// Per-connection tunables projected out of the server budget scope,
// plus the response Date clock seam (tests inject fixed values; an
// empty clock omits the Date engine field entirely).
struct connection_engine_config {
    http1_head_budget head;
    http1_body_budget body;
    http1_outbox_budget outbox;
    http1_response_framer::clock_source clock;

    static connection_engine_config from_budget_limits(
        const server::budget_limits& limits) noexcept {
        connection_engine_config config;
        config.head = http1_head_budget::from_budget_limits(limits);
        config.body = http1_body_budget::from_budget_limits(limits);
        config.outbox = http1_outbox_budget::from_budget_limits(limits);
        return config;
    }
};

// The per-connection engine. Created through make_shared (the task
// tree holds shared_ptr copies of itself).
class connection_engine final
    : public std::enable_shared_from_this<connection_engine> {
 public:
    // Invoked exactly once after every loop finished and the transport
    // was released (the listener engine erases its record there). Must
    // not touch the engine afterwards.
    using stopped_callback = concurrency::unique_function<void()>;

    // @p budget is the scope the connection reserves its seat against
    // (the server root at TASK-108).
    connection_engine(io_poll_backend& backend, worker_pool& pool,
                      const server::route_registry& routes,
                      const server::resource_budget& budget,
                      connection_engine_config config, std::uint64_t id,
                      stopped_callback on_stopped);

    connection_engine(const connection_engine&) = delete;
    connection_engine& operator=(const connection_engine&) = delete;

    // Reserves the connection seat and spawns the task tree. A budget
    // refusal reports the stop immediately (the accept path closes the
    // transport without a response).
    void start();

    // Server-stop path (request_stop): disconnects the live exchange,
    // abandons the outbox, releases the connection. Idempotent,
    // non-blocking, handler-safe (DR-V3-008).
    void shutdown() noexcept;

    std::uint64_t id() const noexcept { return id_; }

    // True while any loop of the task tree is still running.
    bool running() const noexcept;

    // Wakes every parked loop (wake ops coalesce globally; spurious
    // wakes re-check).
    void wake_loops() noexcept;

    static constexpr std::size_t k_read_buffer_bytes = 16384;

 private:
    friend class http1_exchange_sink;
    friend class wake_body_sink;

    // Blocks until a complete head is parseable, the parser failed, the
    // peer hung up, or the server stopped; true only for the complete
    // head.
    static task<bool> wait_for_head(std::shared_ptr<connection_engine> self);
    static task<void> reader_loop(std::shared_ptr<connection_engine> self);
    static task<void> writer_loop(std::shared_ptr<connection_engine> self);
    static task<void> route_loop(std::shared_ptr<connection_engine> self);
    static task<bool> serve_one(std::shared_ptr<connection_engine> self);

    // Stages one exchange's body decoder: seeds it with the drained
    // residue (backpressure leftovers return to the pending tail) and
    // reports a framing failure already visible in the seed.
    bool stage_body(const http1_body_mode& mode, std::string seed);

    // Reader-side byte admission: feeds the active body decoder or the
    // head parser, parking unhandable bytes in the pending tail.
    void absorb(std::string_view data);
    // The head-phase half of absorb; the mutex must be held.
    void absorb_head_locked(std::string_view data);

    // Disconnects the live exchange, if one is being routed.
    void disconnect_current(http::outcome_code reason,
                            std::string detail) noexcept;

    // Marks the connection close-once-drained (error and abort paths).
    void request_close() noexcept;

    // Emits a bare synthesized error (http1_error_synth wire form)
    // through the outbox and marks the close. For the no-routing paths:
    // a failed head, a rejected framing, an exhausted budget.
    void emit_error(std::uint16_t code);

    // Maps a typed failure to its synthesized status: 431 for a budget
    // violation, 501 for an unsupported feature, 400 otherwise.
    static std::uint16_t error_code_for(http::outcome_code code) noexcept;

    // Exchange-tail: end synthesis, keep-alive verdict, pipelined-byte
    // recycle. Returns the keep-alive decision for the route loop.
    bool finish_exchange(wake_body_sink& forwarding,
                         const exchange& routed,
                         http1_exchange_sink& engine_sink,
                         bool body_present);
    // The mutex-guarded half: budget the keep verdict against undrained
    // bodies, recycle parked pipelined bytes, record the close verdict.
    void settle_exchange_state(bool& keep, bool body_present);

    void loop_finished();
    void finalize();

    io_poll_backend& backend_;
    worker_pool& pool_;
    const server::route_registry& routes_;
    io_connection_owner owner_;
    const server::resource_budget& budget_;
    connection_engine_config config_;
    const std::uint64_t id_;
    stopped_callback on_stopped_;

    // --- coordination state (guarded by mu_) ---------------------------
    mutable std::mutex mu_;
    http1_head_parser parser_;
    http1_response_outbox outbox_;
    std::unique_ptr<http1_body_source> body_;
    std::string pending_tail_;   // bytes no consumer could take yet
    std::uint64_t next_sequence_ = 0;
    exchange* current_ = nullptr;   // live exchange being routed
    bool body_active_ = false;
    bool eof_ = false;
    bool shutdown_ = false;
    bool route_done_ = false;
    bool close_after_drain_ = false;
    bool started_ = false;
    int live_loops_ = 0;
    server::reservation seat_;
};

// Engine-side exchange_sink: the one decision seam between a routed
// exchange and the HTTP/1 outbox slot. One per exchange.
class http1_exchange_sink final : public exchange_sink {
 public:
    // @p head must outlive this sink (serve_one frames own it first).
    http1_exchange_sink(std::shared_ptr<connection_engine> engine,
                        http1_response_outbox& outbox,
                        const http::request_head& head);

    // Late binding: the outbox slot opens only after the exchange
    // exists (its disconnect token parks outbox waits); decisions fire
    // strictly later, inside run_route.
    void bind(http1_response_sink& slot) noexcept;

    // True once on_respond serialized the head into the slot.
    bool responded_ok() const noexcept;

    // The keep-alive verdict captured at response commit; close unless
    // a successful final response said otherwise.
    http1_keepalive keepalive() const noexcept;

    // True when the exchange took the upgrade decision (the M8 posture
    // closes cleanly after the committed head).
    bool upgraded() const noexcept;

 private:
    void on_admit(const body_policy& policy) override;
    void on_respond(const http::status& s, const http::fields& f) override;
    void on_upgrade(const ws_upgrade_options& options) override;
    void on_abort() override;

    std::shared_ptr<connection_engine> engine_;
    http1_response_outbox& outbox_;
    const http::request_head& head_;
    http1_response_sink* slot_ = nullptr;
    http1_keepalive keepalive_ = http1_keepalive::close;
    bool responded_ = false;
    bool upgraded_ = false;
};

// Forwarding body_sink between the exchange's response writer and the
// outbox slot: every accepted push/end wakes the parked writer loop,
// and the accepted end marker records end-of-body for the engine's
// synthesis step (the outbox surface is deliberately not widened).
class wake_body_sink final : public body_sink {
 public:
    explicit wake_body_sink(connection_engine& engine) noexcept
        : engine_(engine) { }

    void bind(body_sink& inner) noexcept { inner_ = &inner; }

    // True once push_end was accepted through this wrapper.
    bool ended() const noexcept { return ended_; }

    body_push_result push(std::span<const std::byte> from) override;
    body_push_result push_end(const http::fields& trailers) override;
    const http::outcome& failure() const noexcept override {
        return inner_->failure();
    }
    void park(body_write_wait& wait) override { inner_->park(wait); }
    void unpark(body_write_wait& wait) override { inner_->unpark(wait); }

 private:
    connection_engine& engine_;
    body_sink* inner_ = nullptr;
    bool ended_ = false;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_CONNECTION_ENGINE_HPP_
