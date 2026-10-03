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

// TASK-102 Step 4: a consumer including the v3 exchange area —
// <httpserver/exchange.hpp>, <httpserver/http/status.hpp> and
// <httpserver/server/routes.hpp> — without HTTPSERVER_COMPILATION (or
// any other build/TLS configuration macro) must compile and link
// cleanly. The v3 exchange area is header-only, so the empty LDADD of
// this target is itself part of the contract. Every public exchange
// member is named through an exchange& (the shape route handlers
// receive); the test passes by virtue of compiling and linking.

#include <cstdint>
#include <string>
#include <vector>

#include <type_traits>
#include <httpserver/exchange.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/routes.hpp>

static_assert(httpserver::http::status::from_code(200).with_shoutcast().is_shoutcast());
static_assert(httpserver::http::status::from_code(200).with_shoutcast().code() == 200);
static_assert(httpserver::http::status::from_code(200).with_shoutcast()
              == httpserver::http::status::from_code(200));

int use_v3_exchange_types(httpserver::exchange& x) {
    using httpserver::body_policy;
    using httpserver::exchange_state;
    using httpserver::task;
    using httpserver::ws_upgrade_options;
    namespace http = httpserver::http;

    // Response status values: validated codes and category predicates.
    const http::status ok = http::status::from_code(200);
    const std::uint16_t code = ok.code();
    const bool is_valid = ok.valid();
    const bool categories = ok.informational() || ok.success()
        || ok.redirection() || ok.client_error() || ok.server_error();
    const http::status invalid;
    const bool invalid_default = !invalid.valid();
    const bool same = ok == http::status::from_code(200)
        && ok != http::status::from_code(404);

    // Decision payload types.
    body_policy policy;
    policy.max_buffer_bytes = 4096;
    const std::uint64_t buffer_cap = policy.max_buffer_bytes;
    ws_upgrade_options upgrade;
    upgrade.subprotocols.push_back("chat.example");
    const std::size_t offered = upgrade.subprotocols.size();

    // The exchange's observation surface.
    const http::request_head& head = x.head();
    const exchange_state state = x.state();
    const bool terminal = x.terminal();
    const bool suspended = x.suspended();
    const bool disconnected = x.disconnected();
    const http::outcome& reason = x.disconnect_reason();
    const std::uint64_t connection = x.connection_id();
    const httpserver::stop_token cancellation = x.cancellation();

    // The header-time decisions; outcomes are the only way they fail.
    const bool responded =
        x.respond(ok, http::fields()).ok();
    const bool admitted = x.admit_body(policy).ok();
    httpserver::resume_signal signal;
    const bool waiting = x.suspend(signal).ok();
    auto upgrade_task = x.upgrade(upgrade);
    static_assert(std::is_same_v<decltype(upgrade_task), task<httpserver::websocket_upgrade_result>>);
    const bool aborted = x.abort().ok();
    const bool gone = x.disconnect(http::outcome_code::connection_closed,
                                   "peer left").ok();

    // Route registration still fits the canonical handler shape.
    httpserver::server::route_registry registry;
    const bool routed = registry.route(
        http::method::known(http::method_id::get), "/users/{id}",
        [](httpserver::exchange&) -> task<void> { co_return; }).ok();

    const bool all_good =
        code == 200 && is_valid && categories && invalid_default && same
        && buffer_cap == 4096 && offered == 1
        && head.request_protocol == http::protocol::http_1_0
        && state == exchange_state::head && !terminal && !suspended
        && !disconnected && reason.ok() && connection == 0
        && !cancellation.stop_requested()
        && (responded || admitted || waiting || aborted || gone)
        && routed;
    return all_good ? 0 : 1;
}

int main() {
    // Nothing runs: constructing an exchange requires the engine, and
    // this sentinel must never need one. Compiling is the contract.
    return 0;
}
