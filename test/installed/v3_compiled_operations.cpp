/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <cstddef>
#include <utility>

#include <httpserver.hpp>

bool compiled_operations_worker(httpserver::manual_executor& executor);

int main() {
    namespace http = httpserver::http;
    namespace server = httpserver::server;
    namespace net = httpserver::net;
    namespace auth = httpserver::auth;
    namespace forms = httpserver::forms;
    namespace websocket = httpserver::websocket;
    using httpserver::manual_executor;
    using httpserver::current_executor;
    using httpserver::response_definition;
    http::fields fields;
    fields.append("X-Order", "one");
    fields.append("x-order", "two");
    if (fields.all("X-ORDER").size() != 2 || fields.entries().size() != 2) return 1;
    fields.replace("x-order", "three");
    if (fields.first("X-Order") != "three") return 2;

    manual_executor executor;
    if (!compiled_operations_worker(executor) || current_executor() != nullptr) return 3;
    server::server_options options;
    options.add_listener({"127.0.0.1", 0, false});
    if (!options.validate().ok()) return 4;
    server::budget_limits limits;
    limits.set(server::resource::routes, 4);
    auto budget = server::resource_budget::root(limits);
    server::reservation seat;
    if (!budget.reserve(server::resource::routes, 1, seat).ok()) return 5;
    seat.release();
    if (budget.in_use(server::resource::routes) != 0) return 6;
    server::route_pattern pattern;
    if (!server::route_pattern::parse("/items/{id}", pattern).ok()) return 7;
    server::route_registry registry;
    if (!server::route_registry::create(budget, registry).ok()) return 8;
    if (!registry.route(http::method::known(http::method_id::get), "/items/{id}",
            server::make_sync_route([](const auto&, auto) {
                return server::sync_response{};
            }, 64)).ok()) return 9;

    response_definition definition;
    if (!response_definition::owned_bytes(http::status::from_code(200), fields,
            {std::byte{1}, std::byte{2}}, definition).ok()) return 10;
    if (!definition.valid()) return 11;
    const auto address = net::parse_address("127.0.0.1");
    if (!address || address->to_string() != "127.0.0.1") return 12;
    server::peer_policy policy;
    if (!policy.deny("192.0.2.0/24").ok() || !policy.armed()) return 13;
    server::tls_credentials credentials;
    server::hook_bus hooks;
    if (hooks.any_hooks(server::hook_phase::request_received)) return 14;
    auth::basic_auth_policy basic;
    if (!auth::basic_auth_policy::create("realm", "user", "pass", basic).ok()) return 15;
    http::request_head authenticated;
    authenticated.head_fields.append("Authorization", "Basic dXNlcjpwYXNz");
    if (!basic.check(authenticated).allowed()) return 16;
    auth::digest_auth_policy digest;
    if (!auth::digest_auth_policy::create("realm", "user", "pass", {}, digest).ok()) return 17;
    forms::urlencoded_limits form_limits;
    if (!forms::urlencoded_limits::create(64, 4, form_limits).ok()) return 18;
    forms::form_fields decoded;
    const std::array<std::byte, 3> form{std::byte{'a'}, std::byte{'='}, std::byte{'b'}};
    if (!forms::decode_urlencoded(form, form_limits, decoded).ok() || decoded.value("a") != "b") return 19;
    forms::multipart_limits multipart_limits;
    if (!forms::multipart_limits::create(64, 4, 64, 64, multipart_limits).ok()) return 20;
    forms::temp_file_part_sink multipart_sink;
    if (multipart_sink.take_fields().size() != 0) return 21;
    websocket::session session;
    if (!session.close().ok()) return 22;
    return 0;
}
