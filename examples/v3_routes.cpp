/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/server/server.hpp>
#include "./v3_example_host.hpp"
#include "./v3_route_handlers.hpp"

int main(int argc, char** argv) {
    namespace http = httpserver::http;
    namespace srv = httpserver::server;
    srv::server_options options;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    for (auto method : {http::method_id::get, http::method_id::head}) {
        if (!server.route_sync(http::method::known(method), "/hello",
                               v3_example::hello, v3_example::body_cap).ok()) return 1;
    }
    if (!server.route(http::method::known(http::method_id::post), "/echo",
                      v3_example::echo).ok()) return 1;
    if (!server.route(http::method::known(http::method_id::post), "/stop",
                      [&server](httpserver::exchange&) -> httpserver::task<void> {
                          // Initiation cancels active exchanges, including this one.
                          server.request_stop();
                          co_return;
                      }).ok()) return 1;
    return v3_example::run(server, argc, argv);
}
