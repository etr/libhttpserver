/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier:
 * LGPL-2.1-or-later */
#ifndef TEST_SUPPORT_HTTP3_SEMANTIC_ROUTES_HPP_
#define TEST_SUPPORT_HTTP3_SEMANTIC_ROUTES_HPP_
#include <algorithm>
#include <array>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <httpserver/exchange.hpp>
#include <httpserver/server/routes.hpp>
namespace h3semantic {
namespace h = httpserver;
using observer = std::function<void(const char*, h::exchange&, std::size_t)>;
inline h::task<void> echo(h::exchange& x, observer observe) {
    observe("entered", x, 0);
    x.admit_body({65536});
    std::vector<std::byte> body;
    std::array<std::byte, 3> bytes{};
    for (;;) {
        auto part = co_await x.body().read_some(bytes);
        if (!part.status.ok()) co_return;
        if (part.end_of_body) break;
        if (body.size() + part.data.size() > 65536) co_return;
        body.insert(body.end(), part.data.begin(), part.data.end());
        observe("read", x, part.data.size());
    }
    h::http::fields fields;
    fields.append("trailer", "x-reply");
    fields.append("x-method", x.head().request_method.name());
    fields.append("x-target", x.head().raw_target);
    fields.append("x-route", x.head().route_path);
    fields.append("x-host", *x.head().head_fields.first("host"));
    for (auto value : x.head().head_fields.all("x-repeat")) fields.append("x-repeat", value);
    if (auto value = x.body().trailers().first("x-end")) fields.append("x-request-trailer", *value);
    x.start_response(h::http::status::from_code(200), fields);
    for (std::size_t at = 0; at < body.size(); at += 3) {
        observe("write_started", x, 0);
        auto result = co_await x.writer().write(std::span(body).subspan(at, std::min(std::size_t{3}, body.size() - at)));
        if (!result.status.ok()) co_return;
        observe("write_completed", x, 0);
    }
    h::http::fields trailers;
    trailers.append("x-reply", "done");
    co_await x.writer().finish(trailers);
    observe("finished", x, 0);
}
inline h::task<void> large(h::exchange& x, observer observe) {
    observe("entered", x, 0);
    h::http::fields fields;
    fields.append("trailer", "x-reply");
    x.start_response(h::http::status::from_code(200), fields);
    std::array<std::byte, 4096> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(i % 256);
    for (unsigned i = 0; i < 128; ++i) {
        observe("write_started", x, 0);
        auto result = co_await x.writer().write(bytes);
        if (!result.status.ok()) co_return;
        observe("write_completed", x, 0);
    }
    h::http::fields trailers;
    trailers.append("x-reply", "done");
    co_await x.writer().finish(trailers);
    observe("finished", x, 0);
}
inline void install(h::server::route_registry& routes, observer observe) {
    auto status = routes.route(h::http::method::known(h::http::method_id::post), "/semantic", [observe](h::exchange& x) { return echo(x, observe); });
    if (!status.ok()) throw std::runtime_error("semantic route refused");
    status = routes.route(h::http::method::known(h::http::method_id::get), "/large", [observe](h::exchange& x) { return large(x, observe); });
    if (!status.ok()) throw std::runtime_error("large route refused");
}
}  // namespace h3semantic
#endif  // TEST_SUPPORT_HTTP3_SEMANTIC_ROUTES_HPP_
