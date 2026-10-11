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

#include "config.h"
#include <httpserver/detail/resource_scope.hpp>
#include <httpserver/detail/listener_engine.hpp>
#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_credentials.hpp>
#include <httpserver/detail/tls_credentials_access.hpp>
#include <httpserver/detail/tls_socket_backend.hpp>
#endif

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/io_poll_sys.hpp>
#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/drain_scope.hpp>

namespace httpserver {

namespace detail {

namespace {
#ifdef NATIVE_V3_TLS
struct tls_admission_lifetime {
    std::shared_ptr<drain_scope> scope_owner;
    drain_scope::unit counted;
    std::shared_ptr<server::reservation> seat;
    server::reservation timer, storage;
    tls_admission_lifetime(std::shared_ptr<drain_scope> scope,
            std::shared_ptr<server::reservation> connection,
            server::reservation timer_seat, server::reservation storage_seat)
        : scope_owner(std::move(scope)), counted(*scope_owner), seat(std::move(connection)),
          timer(std::move(timer_seat)), storage(std::move(storage_seat)) {}
};
#endif
}  // namespace

listener_engine::listener_engine(io_socket_backend& backend, worker_pool& pool,
                                 const server::route_registry& routes,
                                 const server::hook_bus& hooks,
                                 const server::resource_budget& budget,
                                 const server::peer_policy& peers,
                                 drain_scope& scope,
                                 connection_engine_config config)
    : backend_(backend),
      pool_(pool),
      routes_(routes),
      hooks_(hooks),
      peers_(peers),
      owner_(pool),
      budget_(child_budget(budget)),
      scope_(scope),
      config_(std::move(config)) {
}

http::outcome listener_engine::listen(
    const server::listener_options& endpoint, std::size_t index,
    std::shared_ptr<tls_psk_runtime> runtime, std::shared_ptr<const tls_credentials_binding> binding) {
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
    {
        std::lock_guard<std::mutex> lock(mu_);
        id_ = id;
        pending_listener_ = transport;
        endpoint_ = endpoint;
        handshake_runtime_ = std::move(runtime);
        tls_binding_ = std::move(binding);
        bound_port_ = port;
        bound_ = true;
    }
    return http::outcome::okay();
}

void listener_engine::activate() {
    {
        std::lock_guard lock(mu_);
        if (stopped_ || activated_ || !bound_) return;
        backend_.adopt_listener(id_, std::exchange(pending_listener_, pollsys::k_invalid_socket));
        activated_ = true;
    }
    spawn(pool_, accept_loop(shared_from_this()), [](task_result<void>) {});
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
void listener_engine::quiesce(std::chrono::steady_clock::time_point server_deadline) noexcept {
    bool release = false;
    std::vector<std::shared_ptr<connection_engine>> live;
    {
        std::lock_guard<std::mutex> lock(mu_);
        release = !stopped_;
        stopped_ = true;
        tls_binding_.reset();
        if (pending_listener_ != pollsys::k_invalid_socket) {
            pollsys::close_socket(pending_listener_);
            bound_port_.store(0, std::memory_order_release);
        }
        collect_live_locked(live);
    }
    // Listener first (a pending accept completes connection_closed and
    // the loop exits; a racing accept that slips through releases its
    // own transport in adopt_accepted), then every live engine.
    if (release) backend_.release_connection(id_);
    for (const std::shared_ptr<connection_engine>& engine : live) {
        engine->quiesce(server_deadline);
    }
}

void listener_engine::request_stop(http::outcome_code reason) noexcept {
    bool release = false;
    std::vector<std::shared_ptr<connection_engine>> live;
    {
        std::lock_guard<std::mutex> lock(mu_);
        release = !stopped_;
        stopped_ = true;
        tls_binding_.reset();
        if (pending_listener_ != pollsys::k_invalid_socket) {
            pollsys::close_socket(pending_listener_);
            bound_port_.store(0, std::memory_order_release);
        }
        collect_live_locked(live);
    }
    // Same release-once rule as quiesce(); the shutdown pass itself
    // always runs (each engine's own guard makes it idempotent), so a
    // drain's deadline expiry hard-stops even already-quiesced engines.
    if (release) backend_.release_connection(id_);
    for (const std::shared_ptr<connection_engine>& engine : live) {
        engine->shutdown(reason);
    }
}

task<void> listener_engine::accept_loop(
    std::shared_ptr<listener_engine> self) {
    for (;;) {
        accept_operation op(self->owner_, self->id_);
        op.submit(self->backend_);
        const io_result r = co_await std::move(op);
        if (r.code != http::outcome_code::ok) {
            if (r.code != http::outcome_code::cancelled && r.code != http::outcome_code::connection_closed) {
                server::diagnostic_event event{server::diagnostic_kind::connection_rejected, server::diagnostic_reason::transport, r.code};
                event.listener_id = self->id_;
                diagnostics_access::record(self->config_.diagnostics, event);
            }
            co_return;
        }
        self->adopt_accepted(r.accepted_id, r.peer);
    }
}

void listener_engine::adopt_accepted(std::uint64_t connection,
                                     const net::peer_address& peer) {
    // The policy gate runs before any engine exists (TASK-119 plan
    // D1): classify() is one atomic load when the policy is unarmed.
    // The verdict is fixed here, THEN the observation fires -- a
    // throwing or responding hook cannot change the outcome.
    const server::peer_verdict verdict = peers_.classify(peer);
    server::accept_decision_ctx seen{peer, verdict.accepted,
                                     verdict.reason};
    (void)hooks_.fire<server::hook_phase::accept_decision>(seen);
    if (!verdict.accepted) {
        server::diagnostic_event event{server::diagnostic_kind::connection_rejected, server::diagnostic_reason::peer_policy, http::outcome_code::peer_refused};
        event.listener_id = id_;
        event.connection_id = connection;
        diagnostics_access::record(config_.diagnostics, event);
        // Zero application bytes: the transport is released unread
        // (the budget-refusal shape; the refusal wire behavior is a
        // pinned migration exception).
        backend_.release_connection(connection);
        return;
    }
    std::shared_ptr<connection_engine> engine;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stopped_) {
            // Stop raced the accept: nobody owns this transport anymore.
            backend_.release_connection(connection);
            return;
        }
        auto connection_budget = child_budget(budget_);
        connection_admission admission;
        admission.seat = std::make_shared<server::reservation>();
        if (!connection_budget.reserve(server::resource::connections, 1, *admission.seat).ok()) {
            server::diagnostic_event event{server::diagnostic_kind::connection_rejected,
                server::diagnostic_reason::capacity, http::outcome_code::limit_exceeded};
            event.listener_id = id_;
            event.connection_id = connection;
            diagnostics_access::record(config_.diagnostics, event);
            backend_.release_connection(connection);
            return;
        }
        std::shared_ptr<io_socket_backend> transport;
        try {
#ifdef NATIVE_V3_TLS
            if (endpoint_.tls) {
                server::reservation timer, storage;
                // Three fixed record buffers cover input, provider-step output
                // and in-flight ciphertext. The lease also retains admission
                // and the drain count until child/provider retirement.
                if (!connection_budget.reserve(server::resource::timers, 1, timer).ok()
                        || !connection_budget.reserve(server::resource::body_buffer_bytes, 3 * 16384, storage).ok()) {
                    backend_.release_connection(connection);
                    return;
                }
                admission.retirement = std::make_shared<tls_admission_lifetime>(
                    config_.drain_owner, admission.seat, std::move(timer), std::move(storage));
                const auto snapshot = tls_credentials_access::acquire(*endpoint_.credentials);
                tls_handshake_context handshake{tls_transport::tcp, bound_port(), config_.diagnostics};
                handshake.runtime = handshake_runtime_;
                handshake.lifetime = admission.retirement;
                handshake.allow_http11 = config_.protocols.contains(http::protocol::http_1_1);
                handshake.allow_http2 = config_.protocols.contains(http::protocol::http_2);
                transport = std::make_shared<tls_socket_backend>(backend_, pool_, connection,
                    snapshot->select_default(), std::move(handshake));
            }
#endif
            // The reserved seat bounds TLS allocation and counts the handshake
            // in the same engine scope as established H1 work.
            engine = std::make_shared<connection_engine>(
                backend_, pool_, routes_, hooks_, connection_budget, scope_, config_,
                connection, [self = shared_from_this(), connection] { self->erase(connection); }, peer,
                &peers_, std::move(transport), std::move(admission));
            connections_.emplace(connection, engine);
        } catch (...) {
            backend_.release_connection(connection);
            return;
        }
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
