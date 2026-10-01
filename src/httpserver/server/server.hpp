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
// the workers. Full drain-ticket semantics arrive with TASK-110;
// v3.0.0 ships the stop-initiation half.
//
// This header is NOT part of the v2 umbrella <httpserver.hpp>: the
// native server is an additive surface and v2 consumers are unaffected.

#ifndef SRC_HTTPSERVER_SERVER_SERVER_HPP_
#define SRC_HTTPSERVER_SERVER_SERVER_HPP_

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
