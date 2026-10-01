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

// TASK-108: the native server's listener engine. One listener_engine
// per configured endpoint: it binds the transport through the one
// pollsys helper that owns platform divergence, adopts it in the poll
// backend, and runs one accept-loop task on the worker pool. Every
// accepted connection gets its own connection_engine (the backend
// registers the accepted transport under a fabricated id before the
// accept operation completes, so the engine only ever sees ids).
//
// Teardown mirrors the server's stop split (DR-V3-008): request_stop()
// releases the listener transport (a pending accept completes
// connection_closed and the loop exits) and shuts every live
// connection engine down -- non-blocking, idempotent, safe from any
// thread including inside a handler. Engine stop callbacks erase their
// records; the server's stop() drains the pool so every record is
// erased before the members die. Full drain-ticket semantics stay with
// TASK-110; TASK-108 ships the stop-initiation half.
#if !defined(HTTPSERVER_COMPILATION)
#error "listener_engine.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_LISTENER_ENGINE_HPP_
#define SRC_HTTPSERVER_DETAIL_LISTENER_ENGINE_HPP_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <httpserver/concurrency/task.hpp>
#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_operation.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/worker_pool.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace detail {

class listener_engine final
    : public std::enable_shared_from_this<listener_engine> {
 public:
    listener_engine(io_poll_backend& backend, worker_pool& pool,
                    const server::route_registry& routes,
                    const server::resource_budget& budget,
                    connection_engine_config config);

    listener_engine(const listener_engine&) = delete;
    listener_engine& operator=(const listener_engine&) = delete;

    // Binds @p endpoint (address spellings per the options contract,
    // port 0 = ephemeral), adopts the listener in the backend, and
    // spawns the accept loop. @p index names the listener in the
    // server's configuration order and derives its transport id (kept
    // out of the backend's fabricated-connection id range). Fails typed
    // when the bind does; the transport is closed either way.
    http::outcome listen(const server::listener_options& endpoint,
                         std::size_t index);

    // The resolved host-order port (0 until listen() succeeds).
    std::uint16_t bound_port() const noexcept { return bound_port_; }

    // Stop initiation: release the listener transport, shut every live
    // connection engine down. Non-blocking, idempotent, handler-safe.
    void request_stop() noexcept;

 private:
    static task<void> accept_loop(std::shared_ptr<listener_engine> self);
    // Builds and registers the engine for one accepted connection (the
    // backend already registered its transport under @p connection).
    void adopt_accepted(std::uint64_t connection);
    // A connection engine's stopped callback: erases its record.
    void erase(std::uint64_t connection) noexcept;

    // Listener transport ids sit far above the backend's fabricated
    // connection ids (a fresh counter from 1), so the two never alias.
    static constexpr std::uint64_t k_listener_id_base = 1ULL << 62;

    io_poll_backend& backend_;
    worker_pool& pool_;
    const server::route_registry& routes_;
    io_connection_owner owner_;
    const server::resource_budget& budget_;
    connection_engine_config config_;
    std::uint64_t id_ = 0;
    std::uint16_t bound_port_ = 0;

    mutable std::mutex mu_;
    std::unordered_map<std::uint64_t,
                       std::shared_ptr<connection_engine>> connections_;
    bool bound_ = false;      // guarded by mu_
    bool stopped_ = false;    // guarded by mu_
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_LISTENER_ENGINE_HPP_
