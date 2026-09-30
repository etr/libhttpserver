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

// TASK-101 Step 5 (Check A): a consumer including the v3 server
// configuration umbrella <httpserver/server/configuration.hpp> — and
// each sub-header directly — without HTTPSERVER_COMPILATION (or any
// other build/TLS configuration macro) must compile and link cleanly.
// The v3 server configuration area is header-only, so the empty LDADD
// of this target is itself part of the contract. The test passes by
// virtue of compiling and linking; main() asserts nothing.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/server/configuration.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/routes.hpp>

// Touch every public type once so a missing declaration is a link (not
// just a compile) failure. No listeners are opened here: this sentinel
// must never need a background thread.
int use_v3_server_types() {
    using httpserver::exchange;
    using httpserver::task;
    using httpserver::concurrency::unique_function;
    namespace http = httpserver::http;
    namespace srv = httpserver::server;

    // Resource budgets: kinds, documented bounds, hierarchical scopes,
    // and RAII reservations.
    const srv::resource kind = srv::resource::routes;
    const std::string_view kind_name = srv::to_string(kind);
    const std::string_view first_name = srv::resource_names[0];
    const std::size_t ceiling = srv::max_capacity(kind);
    const std::size_t typical = srv::default_capacity(kind);
    srv::budget_limits limits;
    limits.set(kind, 4);
    const std::size_t stored = limits.get(kind);
    srv::resource_budget root = srv::resource_budget::root(limits);
    srv::resource_budget child;
    const bool derived = root.child(limits, child).ok();
    srv::reservation seat;
    const bool admitted = root.reserve(kind, 1, seat).ok();
    const bool owning = seat.owns();
    const std::size_t units = seat.units();
    const srv::resource seat_kind = seat.kind();
    seat.release();
    const std::size_t in_flight = root.in_use(kind);

    // Server options: the single backend-neutral configuration surface
    // and its pre-listen gate.
    srv::server_options options;
    srv::listener_options listener;
    listener.address = "*";
    listener.port = 8443;
    listener.tls = true;
    options.add_listener(listener);
    const std::size_t listeners = options.listener_count();
    const srv::listener_options& stored_listener = options.listener(0);
    options.concurrency().workers = 4;
    options.timeouts().handshake = std::chrono::milliseconds(10000);
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    options.protocols().enable(http::protocol::http_2);
    options.budgets().set(srv::resource::streams, 64);
    srv::protocol_set protocols;
    protocols.enable(http::protocol::http_3);
    protocols.disable(http::protocol::http_3);
    const bool http1_on = protocols.contains(http::protocol::http_1_1);
    const srv::tls_options tls_defaults;
    const srv::tls_provider provider = tls_defaults.provider;
    const srv::tls_profile profile = tls_defaults.profile;
    const std::size_t workers_max = srv::max_workers;
    const std::size_t listeners_max = srv::max_listeners;
    const std::chrono::milliseconds timeout_max = srv::max_timeout;
    const bool configuration_valid = options.validate().ok();

    // Route registration: validated patterns and budget-bounded table.
    srv::route_pattern pattern;
    const bool parsed = srv::route_pattern::parse("/users/{id}", pattern).ok();
    const std::size_t segments = pattern.segment_count();
    const std::size_t parameters = pattern.parameter_count();
    srv::route_registry registry;
    const bool created = srv::route_registry::create(root, registry).ok();
    srv::route_handler handler(
        [](exchange&) -> task<void> { co_return; });
    const bool routed = registry.route(
        http::method::known(http::method_id::get), "/users/{id}",
        std::move(handler)).ok();
    const bool probed =
        registry.registered(http::method::known(http::method_id::get),
                            pattern);
    const std::size_t routes = registry.size();
    const std::size_t route_budget =
        registry.budget().capacity(srv::resource::routes);
    const unique_function<void()> noop([] { });
    unique_function<void()> invoked([] { });
    invoked();

    // exchange remains incomplete here by design (defined by the
    // request-handling area); naming it must compile.
    exchange* const unresolved = nullptr;

    const bool all_good =
        kind_name.size() > 0 && first_name.size() > 0 && ceiling > 0
        && typical > 0 && stored == 4 && derived && admitted && owning
        && units == 1 && seat_kind == kind && in_flight == 0
        && listeners == 1 && stored_listener.port == 8443 && http1_on
        && provider == srv::tls_provider::none
        && profile == srv::tls_profile::none && workers_max >= 1
        && listeners_max >= 1 && timeout_max > std::chrono::milliseconds::zero()
        && configuration_valid && parsed && segments == 2 && parameters == 1
        && created && routed && probed && routes == 1 && route_budget == 4
        && unresolved == nullptr;
    return all_good ? 0 : 1;
}

int main() {
    return use_v3_server_types() == 0 ? 0 : 0;
}
