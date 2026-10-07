/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_HTTP2_WEBSOCKET_FIXTURE_HPP_
#define TEST_UNIT_HTTP2_WEBSOCKET_FIXTURE_HPP_
#include <optional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/exchange.hpp>
#include "./http2_request_fixture.hpp"
namespace h2ws {
namespace h = httpserver;
namespace http = h::http;
inline std::vector<h::detail::hpack_field> connect(std::string path = "/chat") {
    return {{":method", "CONNECT"}, {":protocol", "websocket"}, {":scheme", "https"},
        {":authority", "example.test"}, {":path", std::move(path)}, {"sec-websocket-version", "13"}};
}
inline std::vector<std::uint8_t> masked(unsigned opcode, std::string_view text) {
    std::vector<std::uint8_t> out{static_cast<std::uint8_t>(128 | opcode), static_cast<std::uint8_t>(128 | text.size()), 1, 2, 3, 4};
    for (std::size_t i = 0; i < text.size(); ++i) out.push_back(static_cast<std::uint8_t>(text[i]) ^ out[2 + i % 4]);
    return out;
}
inline std::vector<std::uint8_t> data(const std::vector<std::uint8_t>& wire, std::uint32_t id) {
    std::vector<std::uint8_t> out;
    for (const auto& f : h2test::frames(wire)) if (f.type == 0 && f.stream == id) h2test::append(out, f.payload);
    return out;
}
struct fixture {
    h::server::resource_budget budget = h2test::budget();
    h::server::route_registry routes;
    h::manual_executor executor;
    h::detail::hpack_encoder encoder{budget};
    std::unique_ptr<h::detail::http2_request_engine> engine;
    std::vector<h::websocket::session> sessions;
    h::ws_upgrade_options options;
    unsigned refusals = 0;
    explicit fixture(h::detail::http2_request_limits limits = {}) {
        if (!h::server::route_registry::create(budget, routes).ok()) throw std::runtime_error("routes");
        routes.route(http::method::known(http::method_id::connect), "/chat", [this](h::exchange& x) -> h::task<void> {
            auto upgraded = co_await x.upgrade(options);
            if (upgraded.status.ok()) {
                sessions.push_back(std::move(*upgraded.session));
            } else {
                ++refusals; x.respond(upgraded.rejection_status, upgraded.rejection_fields);
            }
        });
        routes.route(http::method::known(http::method_id::get), "/hello", [](h::exchange& x) -> h::task<void> {
            x.respond(http::status::from_code(204), {}); co_return;
        });
        engine = std::make_unique<h::detail::http2_request_engine>(budget, routes, executor, limits);
    }
    bool start() { h2test::output(*engine); return h2test::feed(*engine, h2test::preface()); }
    bool open(std::uint32_t id = 1, std::vector<h::detail::hpack_field> fields = connect()) {
        auto ok = h2test::feed(*engine, h2test::frame(1, 4, id, h2test::encode(encoder, fields)));
        executor.run_pending(); return ok;
    }
    std::vector<std::uint8_t> output() { executor.run_pending(); return h2test::output(*engine); }
};
}  // namespace h2ws
#endif  // TEST_UNIT_HTTP2_WEBSOCKET_FIXTURE_HPP_
