/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <chrono>
#include <utility>

#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/server.hpp>
#include <httpserver/websocket/session.hpp>

#include "./v3_example_host.hpp"
#include "./v3_example_ws.hpp"

int main(int argc, char** argv) {
    namespace srv = httpserver::server;
    namespace http = httpserver::http;
    srv::server_options options;
    options.concurrency().workers = 2;
    options.timeouts().ws_close = std::chrono::seconds(3);
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    if (!server
             .route(http::method::known(http::method_id::get), "/echo",
                    [](httpserver::exchange& x) -> httpserver::task<void> { co_await v3_example::echo_websocket(x); })
             .ok()) {
        return 1;
    }
    return v3_example::run(server, argc, argv);
}
