/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef EXAMPLES_ROUTE_HANDLERS_HPP_
#define EXAMPLES_ROUTE_HANDLERS_HPP_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <httpserver/exchange.hpp>
#include <httpserver/server/route_sync.hpp>

namespace v3_example {
inline constexpr std::uint64_t body_cap = 1024;

inline httpserver::server::sync_response hello(
        const httpserver::http::request_head&, std::span<const std::byte>) {
    httpserver::server::sync_response response;
    response.status = httpserver::http::status::from_code(200);
    response.fields.append("Content-Type", "text/plain");
    constexpr std::string_view text = "hello from v3\n";
    const auto bytes = std::as_bytes(std::span(text));
    response.body.assign(bytes.begin(), bytes.end());
    return response;
}

inline httpserver::task<void> echo(httpserver::exchange& exchange) {
    namespace http = httpserver::http;
    if (!exchange.admit_body(httpserver::body_policy{body_cap}).ok()) co_return;
    auto collected = co_await exchange.body().collect(body_cap);
    if (!collected.status.ok()) {
        if (collected.status.code() == http::outcome_code::limit_exceeded) {
            http::fields fields;
            fields.append("Content-Length", "0");
            static_cast<void>(exchange.respond(http::status::from_code(413), fields));
        }
        co_return;
    }
    http::fields fields;
    fields.append("Content-Type", "application/octet-stream");
    fields.append("Content-Length", std::to_string(collected.data.size()));
    if (collected.data.empty()) {
        static_cast<void>(exchange.respond(http::status::from_code(200), fields));
        co_return;
    }
    if (!exchange.start_response(http::status::from_code(200), fields).ok()) co_return;
    auto written = co_await exchange.writer().write(collected.data);
    if (!written.status.ok()) co_return;
    co_await exchange.writer().finish();
}
}  // namespace v3_example
#endif  // EXAMPLES_ROUTE_HANDLERS_HPP_
