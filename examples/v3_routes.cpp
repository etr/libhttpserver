/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <stdexcept>
#include <string>
#include <utility>

#include <httpserver/server/server.hpp>

#include "./v3_example_host.hpp"
#include "./v3_example_reply.hpp"
#include "./v3_route_handlers.hpp"

int main(int argc, char** argv) {
    namespace http = httpserver::http;
    namespace srv = httpserver::server;
    srv::server_options options;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    const auto error_page = [](unsigned code, std::string text) {
        srv::hook_response response;
        response.status = http::status::from_code(code);
        auto bytes = std::as_bytes(std::span(text));
        response.body.assign(bytes.begin(), bytes.end());
        return response;
    };
    options.not_found_response([error_page](const auto&) { return error_page(404, "not found\n"); });
    options.method_not_allowed_response([error_page](const auto&) { return error_page(405, "method denied\n"); });
    srv::native_server server(options);
    const auto get = http::method::known(http::method_id::get);
    const auto fixed = [](std::string text) -> srv::route_handler {
        return [text = std::move(text)](httpserver::exchange& x) -> httpserver::task<void> {
            co_await v3_example::reply(x, text);
        };
    };
    // The full tier uses registration order: register the literal first.
    if (!server.route(get, "/items/special", fixed("special\n")).ok() ||
        !server
             .route(get, "/items/{id}",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        co_await v3_example::reply(x, std::string(x.path_args().front().value) + "\n");
                    })
             .ok()) {
        return 1;
    }
    http::method_set methods;
    methods.set(http::method_id::get);
    methods.set(http::method_id::post);
    http::method_set reads;
    reads.set(http::method_id::get);
    if (!server.route(methods, "/methods", fixed("method set\n")).ok() ||
        !server.route_prefix(reads, "/tree", fixed("prefix\n")).ok() ||
        !server.route_prefix(reads, "/tree/deep", fixed("deep\n")).ok() ||
        !server.route(get, "/tree/leaf", fixed("full\n")).ok() ||
        !server.route(http::method::extension("CUSTOM"), "/extension", fixed("custom\n")).ok() ||
        !server
             .route(get, "/throw",
                    [](httpserver::exchange&) -> httpserver::task<void> {
                        throw std::runtime_error("private diagnostic");
                        co_return;
                    })
             .ok() ||
        !server
             .route(get, "/throw-committed",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        if (!x.start_response(http::status::from_code(200), {}).ok()) {
                            co_return;
                        }
                        throw std::runtime_error("private committed diagnostic");
                    })
             .ok()) {
        return 1;
    }
    for (auto method : {http::method_id::get, http::method_id::head}) {
        if (!server.route_sync(http::method::known(method), "/hello", v3_example::hello, v3_example::body_cap).ok()) {
            return 1;
        }
    }
    if (!server.route(http::method::known(http::method_id::post), "/echo", v3_example::echo).ok()) {
        return 1;
    }
    if (!server
             .route(http::method::known(http::method_id::post), "/stop",
                    [&server](httpserver::exchange&) -> httpserver::task<void> {
                        // Initiation cancels active exchanges, including this one.
                        server.request_stop();
                        co_return;
                    })
             .ok()) {
        return 1;
    }
    return v3_example::run(server, argc, argv);
}
