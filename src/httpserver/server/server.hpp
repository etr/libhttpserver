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

// TASK-108: the v3 native server lifecycle (architecture §3.4,
// DR-V3-006). native_server owns, behind a pimpl, one route registry,
// one poll-driven transport engine, one worker pool, and one listener
// engine per configured endpoint; every accepted connection runs its
// own HTTP/1 connection engine on the shared pool. Configuration is
// options-only (PRD-V3N-REQ-014); listen() is gated on validate()
// (REQ-016) and performs every bind.
//
// The stop split follows DR-V3-008: request_stop() only initiates --
// it flips the state, releases the listeners, and disconnects every
// live exchange; it never blocks and is safe to call from inside a
// handler. stop() additionally closes the transport engine and joins
// the workers. begin_drain() is the graceful half (TASK-110): it
// stops the accepting side, applies the HTTP/1 close behavior --
// in-flight work finishes and flushes, pipelined-unstarted heads are
// dropped, idle connections close -- and returns a deadline-bound
// ticket reporting completion or expiry (PRD-V3N-REQ-032).
//
// This header is NOT part of the v2 umbrella <httpserver.hpp>: the
// native server is an additive surface and v2 consumers are unaffected.

#ifndef SRC_HTTPSERVER_SERVER_SERVER_HPP_
#define SRC_HTTPSERVER_SERVER_SERVER_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace server {

// The two-state outcome of a drain ticket wait (PRD-V3N-REQ-032):
// every counted unit completed, or the drain's deadline expired with
// work still counted. A stop-driven completion reports completed --
// the two-state result is the documented posture.
enum class drain_status : std::uint8_t { completed, deadline_expired };

// What one ticket wait observed. remaining is the unit count still
// live at expiry (a pre-cancellation snapshot; 0 when completed).
struct drain_result {
    drain_status status = drain_status::completed;
    std::size_t remaining = 0;
};

// The deadline-bound half of the stop split: begin_drain() hands one
// out. The blocking wait reports what the counted work did -- every
// unit completed, or the budget expired with work still live. The
// ticket shares ownership of the counted scope, so it stays safe to
// wait on even after the server object is destroyed.
class drain_ticket {
 public:
    drain_ticket() noexcept;
    ~drain_ticket();

    drain_ticket(drain_ticket&&) noexcept;
    drain_ticket& operator=(drain_ticket&&) noexcept;

    // Blocking wait bounded by the drain's budget: ok with the
    // observed result; would_deadlock when the calling thread runs
    // work this drain counts (a handler of the same server waiting on
    // its own drain); invalid_state on an empty (default-constructed
    // or moved-from) ticket. request_stop()/stop() racing the wait
    // drive it to completed -- the two-state result is the documented
    // posture.
    http::outcome wait(drain_result& out);

 private:
    friend class native_server;
    class impl;

    explicit drain_ticket(std::unique_ptr<impl> inner);

    std::unique_ptr<impl> impl_;
};

// The owned-engine HTTP/1 server. All methods are thread-safe unless
// noted; lifecycle calls (listen/stop) are expected from one
// controlling thread.
class native_server {
 public:
    // Stores the configuration; no I/O, no threads. The options may be
    // re-read (mutated copies re-validated) until listen().
    explicit native_server(server_options options);

    // stop() if still running.
    ~native_server();

    native_server(const native_server&) = delete;
    native_server& operator=(const native_server&) = delete;

    // Registers one route (pattern and handler semantics per the
    // route_registry contract). Fails invalid_state once listen() ran.
    http::outcome route(const http::method& method, std::string_view pattern,
                        route_handler handler);

    // Validates the whole configuration (REQ-016: the pre-listen gate;
    // no I/O before it passes), binds every configured listener, and
    // starts the accept loops. A typed failure binds nothing that was
    // not already bound and leaves the server stopped; a partially
    // bound listen failure is terminal for this server object.
    http::outcome listen();

    // Stop initiation (DR-V3-008): idempotent, non-blocking, safe from
    // any thread including inside a handler.
    void request_stop() noexcept;

    // request_stop() plus closing the transport engine and joining the
    // workers. Returns once every engine task finished.
    void stop();

    // Drain initiation (PRD-V3N-REQ-032): stop accepting new work,
    // apply the HTTP/1 close behavior to active and pipelined work,
    // and fill @p out with a ticket whose wait() is bounded by
    // @p budget. Nonblocking and handler-safe like request_stop(); it
    // never waits. budget <= 0 is invalid_argument; before listen(),
    // after a stop, or on a second drain it is invalid_state. One
    // drain per server object. is_running() keeps its meaning (a
    // drain is a run-down, not a stop); request_stop()/stop() during
    // the drain drive the ticket to completed.
    http::outcome begin_drain(std::chrono::milliseconds budget,
                              drain_ticket& out);

    // True from a successful listen() until request_stop() (or the
    // destructor) runs.
    bool is_running() const noexcept;

    // The resolved port of listener @p listener_index (0 for an
    // ephemeral-port request reports the bound port; 0 before listen()
    // or for an out-of-range index).
    std::uint16_t get_bound_port(std::size_t listener_index) const noexcept;

 private:
    class impl;

    std::unique_ptr<impl> impl_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_SERVER_HPP_
