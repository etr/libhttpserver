/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string>
#include <utility>

#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/cookie.hpp>

#include "./v3_example_host.hpp"
#include "./v3_example_reply.hpp"
namespace http = httpserver::http;
namespace srv = httpserver::server;
int main(int argc, char** argv) {
    http::cookie invalid;
    invalid.name = "bad name";
    std::string unchanged = "preserved";
    if (http::render_set_cookie(invalid, unchanged).code() != http::outcome_code::invalid_argument ||
        unchanged != "preserved") {
        return 1;
    }
    http::fields demonstration;
    demonstration.append("X-Value", "first");
    demonstration.append("X-Value", "second");
    if (demonstration.first("x-value") != "first" || demonstration.all("x-value").size() != 2) {
        return 1;
    }
    demonstration.replace("X-Value", "replacement");
    if (demonstration.entries().size() != 1) {
        return 1;
    }
    srv::server_options options;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    if (!server
             .route(http::method::known(http::method_id::get), "/inspect",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        const auto& head = x.head();
                        httpserver::forms::form_fields query;
                        const auto mark = head.raw_target.find('?');
                        const std::string_view encoded = mark == std::string::npos
                                                             ? std::string_view{}
                                                             : std::string_view(head.raw_target).substr(mark + 1);
                        const auto decoded =
                            httpserver::forms::decode_urlencoded(std::as_bytes(std::span(encoded)), {256, 8}, query);
                        if (!decoded.ok()) {
                            co_await v3_example::reply(
                                x, "", decoded.code() == http::outcome_code::limit_exceeded ? 413 : 400);
                            co_return;
                        }
                        std::string text = "raw=" + head.raw_target + "\npath=" + head.route_path + "\n";
                        for (const auto& value : head.head_fields.all("X-Repeat")) {
                            text += "header=" + value + "\n";
                        }
                        // Cookie fields stay separate; parsing each preserves repeated pairs.
                        for (const auto& field : head.head_fields.all("Cookie")) {
                            for (const auto& cookie : http::parse_cookie_header(field)) {
                                text += "cookie=" + cookie.name + "=" + cookie.value + "\n";
                            }
                        }
                        for (const auto& [name, value] : query.entries()) {
                            text += name + "=" + value + "\n";
                        }
                        http::fields fields;
                        for (const char* name : {"demo", "second"}) {
                            http::cookie cookie;
                            cookie.name = name;
                            cookie.value = "fixed";
                            cookie.path = "/";
                            cookie.http_only = true;
                            std::string rendered;
                            if (!http::render_set_cookie(cookie, rendered).ok()) {
                                co_return;
                            }
                            fields.append("Set-Cookie", rendered);
                        }
                        co_await v3_example::reply(x, std::move(text), 200, fields);
                    })
             .ok()) {
        return 1;
    }
    return v3_example::run(server, argc, argv);
}
