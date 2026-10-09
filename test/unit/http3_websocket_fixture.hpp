/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_HTTP3_WEBSOCKET_FIXTURE_HPP_
#define TEST_UNIT_HTTP3_WEBSOCKET_FIXTURE_HPP_
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "./http3_request_fixture.hpp"
namespace h3ws {
namespace h = httpserver;
namespace http = h::http;
inline std::vector<h::detail::qpack_field> connect(std::string path = "/chat") {
    return {{":method", "CONNECT"}, {":protocol", "websocket"}, {":scheme", "https"},
        {":authority", "example.test"}, {":path", std::move(path)}, {"sec-websocket-version", "13"}};
}
struct fixture : h3test::request_fixture {
    std::vector<h::websocket::session> sessions;
    h::ws_upgrade_options options;
    std::string selected;
    unsigned refusals = 0;
    http::outcome_code refusal = http::outcome_code::ok;
    bool repeat = false;
    std::function<void()> before_upgrade, after_upgrade;
    http::outcome_code second = http::outcome_code::ok, response_after = http::outcome_code::ok;
    explicit fixture(h::detail::http3_request_limits limits = {}) : request_fixture(limits) {
        routes.route(http::method::known(http::method_id::connect), "/chat", [this](h::exchange& x) -> h::task<void> {
            auto pending = x.upgrade(options);
            if (before_upgrade) before_upgrade();
            auto result = co_await std::move(pending);
            if (after_upgrade) after_upgrade();
            if (result.status.ok()) {
                selected = result.selected_subprotocol;
                sessions.push_back(std::move(*result.session));
                if (repeat) {
                    second = (co_await x.upgrade(options)).status.code();
                    response_after = x.respond(http::status::from_code(204), {}).code();
                }
            } else {
                ++refusals; refusal = result.status.code();
                x.respond(result.rejection_status, result.rejection_fields);
            }
        });
        routes.route(http::method::known(http::method_id::get), "/hello", [](h::exchange& x) -> h::task<void> {
            x.respond(http::status::from_code(204), {}); co_return;
        });
    }
    bool settings(std::optional<unsigned> enabled = 1, std::optional<unsigned> max_section = {}) {
        open(2);
        std::vector<std::byte> payload;
        if (enabled) {
            h3test::integer(payload, 8); h3test::integer(payload, *enabled);
        }
        if (max_section) {
            h3test::integer(payload, 6); h3test::integer(payload, *max_section);
        }
        auto wire = h3test::bytes({0}); auto frame = h3test::frame(4, payload);
        wire.insert(wire.end(), frame.begin(), frame.end());
        return feed(2, wire);
    }
    bool request(std::uint64_t id = 0, std::vector<h::detail::qpack_field> fields = connect(), bool fin = false) {
        open(id); auto ok = feed(id, h3test::headers(fields), fin); executor.run_pending(); return ok;
    }
    h3test::response reply(std::uint64_t id = 0) { drain(); return h3test::decode_response(output[id]); }
};
}  // namespace h3ws
#endif  // TEST_UNIT_HTTP3_WEBSOCKET_FIXTURE_HPP_
