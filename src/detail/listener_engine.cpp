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

// TASK-108: the accept loop and connection bookkeeping of one native
// listener. See listener_engine.hpp for the design contract.

#include <httpserver/detail/listener_engine.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/io_poll_sys.hpp>

namespace httpserver {

namespace detail {

listener_engine::listener_engine(io_poll_backend& backend, worker_pool& pool,
                                 const server::route_registry& routes,
                                 const server::hook_bus& hooks,
                                 const server::resource_budget& budget,
                                 drain_scope& scope,
                                 connection_engine_config config)
    : backend_(backend),
      pool_(pool),
      routes_(routes),
      hooks_(hooks),
      owner_(pool),
      budget_(budget),
      scope_(scope),
      config_(std::move(config)) {
}

http::outcome listener_engine::listen(
    const server::listener_options& endpoint, std::size_t index) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (bound_ || stopped_) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "listener_engine: listen() ran twice");
        }
    }
    std::uint16_t port = 0;
    const pollsys::native_socket_t transport = pollsys::open_listener(
        endpoint.address, endpoint.port, port);
    if (transport == pollsys::k_invalid_socket) {
        return http::outcome(
            http::outcome_code::connection_closed,
            "listener_engine: cannot bind "
                + endpoint.address + ":" + std::to_string(endpoint.port));
    }
    const std::uint64_t id = k_listener_id_base + index;
    backend_.adopt_listener(id, transport);
    {
        std::lock_guard<std::mutex> lock(mu_);
        id_ = id;
        bound_port_ = port;
        bound_ = true;
    }
    spawn(pool_, accept_loop(shared_from_this()),
          [](task_result<void>) { });
    return http::outcome::okay();
}

void listener_engine::collect_live_locked(
        std::vector<std::shared_ptr<connection_engine>>& out) {
    out.reserve(out.size() + connections_.size());
    for (const auto& entry : connections_) {
        out.push_back(entry.second);
    }
}

// The drain path (TASK-110): stop accepting and hand every live
// engine its graceful close. The listener release follows the same
// first-caller rule as request_stop(); the engine-side quiesce guard
// sorts out whatever a racing request_stop() already did.
void listener_engine::quiesce() noexcept {
    bool release = false;
    std::vector<std::shared_ptr<connection_engine>> live;
    {
        std::lock_guard<std::mutex> lock(mu_);
        release = !stopped_;
        stopped_ = true;
        collect_live_locked(live);
    }
    // Listener first (a pending accept completes connection_closed and
    // the loop exits; a racing accept that slips through releases its
    // own transport in adopt_accepted), then every live engine.
    if (release) backend_.release_connection(id_);
    for (const std::shared_ptr<connection_engine>& engine : live) {
        engine->quiesce();
    }
}

void listener_engine::request_stop() noexcept {
    bool release = false;
    std::vector<std::shared_ptr<connection_engine>> live;
    {
        std::lock_guard<std::mutex> lock(mu_);
        release = !stopped_;
        stopped_ = true;
        collect_live_locked(live);
    }
    // Same release-once rule as quiesce(); the shutdown pass itself
    // always runs (each engine's own guard makes it idempotent), so a
    // drain's deadline expiry hard-stops even already-quiesced engines.
    if (release) backend_.release_connection(id_);
    for (const std::shared_ptr<connection_engine>& engine : live) {
        engine->shutdown();
    }
}

task<void> listener_engine::accept_loop(
    std::shared_ptr<listener_engine> self) {
    for (;;) {
        accept_operation op(self->owner_, self->id_);
        op.submit(self->backend_);
        const io_result r = co_await std::move(op);
        if (r.code != http::outcome_code::ok) co_return;
        self->adopt_accepted(r.accepted_id);
    }
}

void listener_engine::adopt_accepted(std::uint64_t connection) {
    std::shared_ptr<connection_engine> engine;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stopped_) {
            // Stop raced the accept: nobody owns this transport anymore.
            backend_.release_connection(connection);
            return;
        }
        engine = std::make_shared<connection_engine>(
            backend_, pool_, routes_, hooks_, budget_, scope_, config_,
            connection, [this, connection] { erase(connection); });
        connections_.emplace(connection, engine);
    }
    // Outside the lock: a budget-refusing engine reports its stop from
    // inside start(), and the erase callback takes the same mutex.
    engine->start();
}

void listener_engine::erase(std::uint64_t connection) noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    static_cast<void>(connections_.erase(connection));
}

}  // namespace detail

}  // namespace httpserver
