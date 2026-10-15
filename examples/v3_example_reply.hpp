/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef EXAMPLES_V3_EXAMPLE_REPLY_HPP_
#define EXAMPLES_V3_EXAMPLE_REPLY_HPP_
#include <string>

#include <httpserver/exchange.hpp>
#include <httpserver/server/route_sync.hpp>
namespace v3_example {
inline httpserver::server::sync_response value(std::string text, unsigned code = 200) {
    httpserver::server::sync_response response;
    response.status = httpserver::http::status::from_code(code);
    response.fields.append("Content-Type", "text/plain");
    auto bytes = std::as_bytes(std::span(text));
    response.body.assign(bytes.begin(), bytes.end());
    return response;
}
inline httpserver::task<void> reply(httpserver::exchange& x, std::string text, unsigned code = 200,
                                    httpserver::http::fields fields = {}) {
    fields.append("Content-Length", std::to_string(text.size()));
    if (!x.start_response(httpserver::http::status::from_code(code), fields).ok()) {
        co_return;
    }
    if (!text.empty() && !(co_await x.writer().write(std::as_bytes(std::span(text)))).status.ok()) {
        co_return;
    }
    co_await x.writer().finish();
}
}  // namespace v3_example
#endif  // EXAMPLES_V3_EXAMPLE_REPLY_HPP_
