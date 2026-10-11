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

// TASK-108: the native_server pimpl. Owns, in destruction order: the
// stop flags, the managed transport engine (its close completes
// every pending operation), the worker pool (drains those
// completions), the listener engines (their connection records are all
// erased by then), the route registry, and the budget scope it
// reserves against. See server/server.hpp for the lifecycle contract.

#include "config.h"
#include <httpserver/server/server.hpp>
#ifdef NATIVE_V3_TLS
#include <httpserver/detail/tls_credentials.hpp>
#include <httpserver/detail/tls_credentials_access.hpp>
#include <httpserver/detail/tls_psk_runtime.hpp>
#include <httpserver/detail/quic_udp_listener.hpp>
#endif

#include <atomic>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/connection_engine.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/io_managed_backend.hpp>
#include <httpserver/detail/listener_engine.hpp>
#include <httpserver/detail/worker_pool.hpp>
#include <httpserver/detail/http2_dispatch.hpp>
#include <httpserver/server/hooks.hpp>
#include <httpserver/server/peer_policy.hpp>
#include <httpserver/server/route_sync.hpp>

namespace httpserver {

namespace server {

namespace engine = httpserver::detail;

class native_server::impl {
 public:
    explicit impl(server_options options)
        : scope_(std::make_shared<engine::drain_scope>(options.diagnostic_channel())),
          options_(std::move(options)),
          budget_(resource_budget::root(options_.budgets())),
          pool_(options_.concurrency().workers),
          backend_(engine::make_socket_backend(options_.loop())) {
        engine::diagnostics_access::attach(budget_, options_.diagnostic_channel());
        registry_state_ = route_registry::create(budget_, registry_);
    }

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    http::outcome route(const http::method& method, std::string_view pattern,
                        route_handler handler) {
        if (running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: route() after listen() is not allowed");
        }
        return registry_.route(method, pattern, std::move(handler));
    }

    http::outcome route(const http::method_set& methods,
                        std::string_view pattern, route_handler handler) {
        if (running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: route() after listen() is not allowed");
        }
        return registry_.route(methods, pattern, std::move(handler));
    }

    http::outcome route_prefix(const http::method_set& methods,
                               std::string_view pattern,
                               route_handler handler) {
        if (running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: route_prefix() after listen() is not"
                " allowed");
        }
        return registry_.route_prefix(methods, pattern, std::move(handler));
    }

    http::outcome route_sync(const http::method& method,
                             std::string_view pattern,
                             sync_route_handler handler,
                             std::uint64_t body_cap) {
        if (running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: route_sync() after listen() is not allowed");
        }
        if (body_cap == 0) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "native_server: route_sync needs a body cap of at least"
                " one byte");
        }
        if (!handler) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "native_server: route_sync handler is empty");
        }
        return registry_.route(
            method, pattern, make_sync_route(std::move(handler), body_cap));
    }

    bool disabled_protocol(std::string_view token) const {
        if (token == "http/1.1") return !options_.protocols().contains(http::protocol::http_1_1);
        if (token == "h2") return !options_.protocols().contains(http::protocol::http_2);
        if (token == "h3") return !options_.protocols().contains(http::protocol::http_3);
        return false;
    }
    http::outcome prelisten_ready() const {
        if (const auto valid = options_.validate(); !valid.ok()) {
            return valid;
        }
        // Build-time provider availability does not imply an implemented
        // transport. Refuse the whole configuration before binding any endpoint.
#ifndef NATIVE_V3_TLS
        if (options_.protocols().contains(http::protocol::http_3) || options_.protocols().contains(http::protocol::http_2)) {
            return {http::outcome_code::not_supported, "native_server: requested TLS/protocol transport is unavailable"};
        }
#endif
        for (std::size_t i = 0; i < options_.listener_count(); ++i) {
            if (options_.listener(i).tls) {
#ifdef NATIVE_V3_TLS
                const auto& credentials = options_.listener(i).credentials;
                const auto snapshot = credentials ? engine::tls_credentials_access::acquire(*credentials) : nullptr;
                if (!snapshot) {
                    return {http::outcome_code::invalid_argument, "native_server: TLS listener needs published credentials"};
                }
                for (const auto& host : snapshot->hosts()) {
                    const bool disallowed = std::any_of(host.alpn.begin(), host.alpn.end(), [&](const auto& token) {
                        return disabled_protocol(token);
                    });
                    if (disallowed) {
                        return {http::outcome_code::invalid_argument, "native_server: credential ALPN requires enabled protocol"};
                    }
                }
#else
                return {http::outcome_code::not_supported,
                        "native_server: requested TLS transport is unavailable"};
#endif
            }
        }
        if (!registry_state_.ok()) return registry_state_;
        return backend_->ready();
    }

    void activate_listeners() {
        for (const auto& listener : listeners_) if (listener) listener->activate();
#ifdef NATIVE_V3_TLS
        for (const auto& listener : udp_listeners_) if (listener) listener->activate();
#endif
    }
    http::outcome bind_listeners(const engine::connection_engine_config& config
#ifdef NATIVE_V3_TLS
        , std::vector<std::shared_ptr<const engine::tls_credentials_binding>>& bindings
#endif
    ) {
        listeners_.resize(options_.listener_count());
#ifdef NATIVE_V3_TLS
        udp_listeners_.resize(options_.listener_count());
#endif
        for (std::size_t i = 0; i < options_.listener_count(); ++i) {
            http::outcome bound;
            bool bound_udp = false;
#ifdef NATIVE_V3_TLS
            if (options_.listener(i).transport == listener_transport::udp) {
                auto listener = std::make_shared<engine::quic_udp_listener>(*backend_, pool_, registry_, hooks_, peers_, budget_, config);
                udp_listeners_[i] = listener;
                bound = listener->listen(options_.listener(i), i, std::move(bindings[i]));
                bound_udp = true;
            }
#endif
            if (!bound_udp) {
                auto listener = std::make_shared<engine::listener_engine>(*backend_, pool_, registry_, hooks_, budget_, peers_, *scope_, config);
                listeners_[i] = listener;
                bound = listener->listen(options_.listener(i), i, handshake_runtime_,
#ifdef NATIVE_V3_TLS
                    std::move(bindings[i]));
#else
                    {});
#endif
            }
            if (!bound.ok()) {
                request_stop(); return bound;
            }
        }
        activate_listeners();
        return {};
    }
    http::outcome listen() {
        if (stop_requested_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: this server was stopped and cannot listen");
        }
        if (running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: listen() ran twice");
        }
        // REQ-016: the validate() gate runs before any bind.
        if (const auto ready = prelisten_ready(); !ready.ok()) {
            return ready;
        }

#ifdef NATIVE_V3_TLS
        std::vector<std::shared_ptr<const engine::tls_credentials_binding>> bindings(options_.listener_count());
        for (std::size_t i = 0; i < options_.listener_count(); ++i) {
            const auto& endpoint = options_.listener(i);
            if (!endpoint.tls) continue;
            engine::tls_credentials_binding policy{options_.tls().profile,
                detail::resolved_client_certificate_mode(options_.tls().profile, options_.tls().client_auth),
                options_.protocols().contains(http::protocol::http_1_1), options_.protocols().contains(http::protocol::http_2),
                options_.protocols().contains(http::protocol::http_3), endpoint.transport == listener_transport::udp};
            const auto bound = engine::tls_credentials_access::bind(*endpoint.credentials, policy, bindings[i]);
            if (!bound.ok()) return bound;
        }
#endif
        engine::connection_engine_config config =
            engine::connection_engine_config::from_budget_limits(options_.budgets());
        config.diagnostics = options_.diagnostic_channel();
        config.protocols = options_.protocols();
        config.drain_owner = scope_;
        config.timeouts = options_.timeouts();
        auto pages = std::make_shared<engine::error_page_factories>();
        pages->not_found = options_.not_found_response();
        pages->method_not_allowed = options_.method_not_allowed_response();
        config.pages = std::move(pages);

        // TASK-119: seed the live policy from the options. V12 judged
        // every spelling with this header's own grammar; the store
        // parses with the library's. A disagreement here would be a
        // grammar split between the two, so it fails listen() typed
        // instead of silently dropping the entry.
        peers_.set_enabled(options_.peer_policy().enabled);
        peers_.set_mode(options_.peer_policy().mode);
        for (const std::string& pattern : options_.peer_policy().deny) {
            const http::outcome seeded = peers_.deny(pattern);
            if (!seeded.ok()) return seeded;
        }
        for (const std::string& pattern : options_.peer_policy().allow) {
            const http::outcome seeded = peers_.allow(pattern);
            if (!seeded.ok()) return seeded;
        }

#ifdef NATIVE_V3_TLS
        for (std::size_t i = 0; i < options_.listener_count(); ++i) {
            if (options_.listener(i).tls && !handshake_runtime_) {
                engine::tls_psk_runtime_options limits;
                limits.handshake_timeout = options_.timeouts().handshake;
                handshake_runtime_ = std::make_shared<engine::tls_psk_runtime>(limits);
            }
        }
#endif
#ifdef NATIVE_V3_TLS
        const auto bound = bind_listeners(config, bindings);
#else
        const auto bound = bind_listeners(config);
#endif
        if (!bound.ok()) return bound;
        backend_->activate_external();
        running_.store(true, std::memory_order_release);
        return http::outcome::okay();
    }

    void request_stop() noexcept {
        stop_requested_.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        for (const std::shared_ptr<engine::listener_engine>& listener :
             listeners_) {
            if (listener) listener->request_stop();
        }
#ifdef NATIVE_V3_TLS
        for (const auto& listener : udp_listeners_) if (listener) listener->request_stop();
#endif
        static_cast<void>(backend_->wake());
    }

    void stop() {
        request_stop();
#ifdef NATIVE_V3_TLS
        if (options_.loop() == loop_mode::external) backend_->begin_retirement();
        for (;;) {
            const auto waiting = std::find_if(udp_listeners_.begin(), udp_listeners_.end(), [](const auto& listener) { return listener && !listener->retired(); });
            if (waiting == udp_listeners_.end()) break;
            if (options_.loop() == loop_mode::external) {
                backend_->retirement_progress(std::chrono::milliseconds(50));
            } else {
                (*waiting)->await_progress(std::chrono::milliseconds(50));
            }
        }
#endif
        // Terminal-fails every pending operation; the completions queue
        // onto the pool, and the drain joins the engines' unwinding.
        static_cast<void>(backend_->close());
#ifdef NATIVE_V3_TLS
        if (handshake_runtime_) {
            handshake_runtime_->stop();
            // Certificate provider steps are bounded and contain no user code.
            // Retire them before closing their completion executor.
            static_cast<void>(handshake_runtime_->drain(std::chrono::steady_clock::now() + options_.timeouts().handshake));
        }
#endif
        pool_.drain_and_join();
    }

    std::vector<std::weak_ptr<engine::listener_engine>> weak_tcp_listeners() const {
        std::vector<std::weak_ptr<engine::listener_engine>> listeners;
        listeners.reserve(listeners_.size());
        for (const auto& listener : listeners_) if (listener) listeners.push_back(listener);
        return listeners;
    }
    void quiesce_listeners(std::chrono::steady_clock::time_point deadline) {
        for (const std::shared_ptr<engine::listener_engine>& listener :
             listeners_) {
            if (listener) listener->quiesce(deadline);
        }
#ifdef NATIVE_V3_TLS
        for (const auto& listener : udp_listeners_) if (listener) listener->quiesce(deadline);
#endif
    }

    // TASK-110: drain initiation (PRD-V3N-REQ-032). Arms the counted
    // scope (deadline + the would_deadlock predicate + the hard-stop
    // cancel hook), then quiesces every listener -- no accepting, live
    // exchanges finish and flush. Never waits; the ticket owns the
    // blocking half.
    http::outcome begin_drain(std::chrono::milliseconds budget) {
        if (budget <= std::chrono::milliseconds::zero()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "native_server: begin_drain needs a positive budget");
        }
        if (stop_requested_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: this server was stopped; no drain");
        }
        if (!running_.load(std::memory_order_acquire)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: begin_drain() before listen() is not"
                " allowed");
        }
        if (drain_begun_.exchange(true)) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "native_server: begin_drain() ran twice");
        }
        // The ticket can outlive this server. Weak listener captures avoid
        // a scope -> listener -> scope ownership cycle while a live
        // connection keeps its listener available for cancellation.
        // Compare executor identity without dereferencing the pool:
        // a retiring provider step may retain the counted scope after stop.
        const auto listeners = weak_tcp_listeners();
        const auto deadline = std::chrono::steady_clock::now() + budget;
        scope_->arm(deadline,
                    [identity = static_cast<const executor*>(&pool_), scope = scope_.get()] { return current_executor() == identity || engine::counted_drain_slot() == scope; },
                    [listeners] {
                        for (const auto& weak : listeners) {
                            if (auto listener = weak.lock()) listener->request_stop(http::outcome_code::timeout);
                        }
                    });
        quiesce_listeners(deadline);
        static_cast<void>(backend_->wake());
        return http::outcome::okay();
    }

    const std::shared_ptr<engine::drain_scope>& scope() const noexcept {
        return scope_;
    }

    bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    std::uint16_t get_bound_port(std::size_t listener_index) const noexcept {
        if (listener_index >= listeners_.size()) return 0;
#ifdef NATIVE_V3_TLS
        if (udp_listeners_[listener_index]) return udp_listeners_[listener_index]->bound_port();
#endif
        return listeners_[listener_index] ? listeners_[listener_index]->bound_port() : 0;
    }

    readiness_driver* readiness() noexcept {
        return options_.loop() == loop_mode::external ? backend_.get() : nullptr;
    }

    hook_bus& hooks() const noexcept { return hooks_; }

    server::peer_policy& peers() const noexcept { return peers_; }

 private:
    // Destruction order (reverse declaration): the flags, the transport
    // engine (close), the pool (drain), the listeners, the registry,
    // the budget, the options, the drain scope. The engine tasks that
    // the close enqueues run during the pool's implicit drain; every
    // listener record is erased before the listeners die, and the scope
    // outlives them all so a finalizing engine's leave() is always
    // in-bounds (a ticket holding a shared_ptr copy may outlive even
    // this).
    std::shared_ptr<engine::drain_scope> scope_;
    server_options options_;
    resource_budget budget_;
    route_registry registry_;
    http::outcome registry_state_;
    // Declared before the listeners and the pool: connection engines
    // reference the bus for their whole live window, so the bus
    // outlives them (reverse destruction order).
    mutable hook_bus hooks_;
    // TASK-119: the server-wide peer policy. Seeded from the options
    // at listen() and live-mutable afterwards (DR-V3-008); declared
    // before the listeners, which hold a reference for their whole
    // live window. peer_policy() (native_server) hands out the
    // consumer-side view.
    mutable server::peer_policy peers_;
    std::shared_ptr<engine::tls_psk_runtime> handshake_runtime_;
    std::vector<std::shared_ptr<engine::listener_engine>> listeners_;
#ifdef NATIVE_V3_TLS
    std::vector<std::shared_ptr<engine::quic_udp_listener>> udp_listeners_;
#endif
    engine::worker_pool pool_;
    std::unique_ptr<engine::io_socket_backend> backend_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> drain_begun_{false};
};

// TASK-110: the ticket's pimpl. It holds the counted scope by shared
// ownership, so a ticket may outlive the server and still wait; the
// scope's own ordering contract makes that late wait well-defined.
class drain_ticket::impl {
 public:
    explicit impl(std::shared_ptr<engine::drain_scope> scope)
        : scope_(std::move(scope)) { }

    std::shared_ptr<engine::drain_scope> scope_;
};

drain_ticket::drain_ticket() noexcept = default;

drain_ticket::drain_ticket(std::unique_ptr<impl> inner)
    : impl_(std::move(inner)) { }

drain_ticket::~drain_ticket() = default;

drain_ticket::drain_ticket(drain_ticket&&) noexcept = default;

drain_ticket& drain_ticket::operator=(drain_ticket&&) noexcept = default;

http::outcome drain_ticket::wait(drain_result& out) {
    if (impl_ == nullptr) {
        return http::outcome(
            http::outcome_code::invalid_state,
            "native_server: wait() on an empty drain ticket");
    }
    return impl_->scope_->wait(out);
}

native_server::native_server(server_options options)
    : impl_(std::make_unique<impl>(std::move(options))) {
}

native_server::~native_server() {
    impl_->stop();
}

http::outcome native_server::route(const http::method& method,
                                   std::string_view pattern,
                                   route_handler handler) {
    return impl_->route(method, pattern, std::move(handler));
}

http::outcome native_server::route(const http::method_set& methods,
                                   std::string_view pattern,
                                   route_handler handler) {
    return impl_->route(methods, pattern, std::move(handler));
}

http::outcome native_server::route_prefix(const http::method_set& methods,
                                          std::string_view pattern,
                                          route_handler handler) {
    return impl_->route_prefix(methods, pattern, std::move(handler));
}

http::outcome native_server::route_sync(const http::method& method,
                                        std::string_view pattern,
                                        sync_route_handler handler,
                                        std::uint64_t body_cap) {
    return impl_->route_sync(method, pattern, std::move(handler), body_cap);
}

http::outcome native_server::listen() { return impl_->listen(); }

void native_server::request_stop() noexcept { impl_->request_stop(); }

void native_server::stop() { impl_->stop(); }

http::outcome native_server::begin_drain(std::chrono::milliseconds budget,
                                         drain_ticket& out) {
    const http::outcome began = impl_->begin_drain(budget);
    if (!began.ok()) return began;
    out = drain_ticket(std::make_unique<drain_ticket::impl>(
        impl_->scope()));
    return began;
}

bool native_server::is_running() const noexcept {
    return impl_->is_running();
}

std::uint16_t native_server::get_bound_port(
        std::size_t listener_index) const noexcept {
    return impl_->get_bound_port(listener_index);
}

readiness_driver* native_server::readiness() noexcept {
    return impl_->readiness();
}

hook_bus& native_server::hooks() const noexcept {
    return impl_->hooks();
}

peer_policy& native_server::peer_policy() const noexcept {
    return impl_->peers();
}

}  // namespace server

}  // namespace httpserver

namespace httpserver::detail {
server::drain_ticket drain_ticket_access::make(std::shared_ptr<drain_scope> scope) {
    return server::drain_ticket(std::make_unique<server::drain_ticket::impl>(std::move(scope)));
}
}  // namespace httpserver::detail
