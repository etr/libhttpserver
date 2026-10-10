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

namespace {
httpserver::task<void> echo(httpserver::exchange& exchange) {
    auto upgraded = co_await exchange.upgrade(httpserver::ws_upgrade_options{});
    if (!upgraded.status.ok()) {
        upgraded.rejection_fields.append("Content-Length", "0");
        static_cast<void>(exchange.respond(upgraded.rejection_status, upgraded.rejection_fields));
        co_return;
    }
    auto session = std::move(*upgraded.session);
    httpserver::resume_signal closed;
    session.on_close([closed](httpserver::websocket::close_info) mutable { closed.signal(); });
    bool sending = true;
    while (sending) {
        auto received = co_await session.receive();
        if (!received.value) break;
        for (;;) {
            auto sent = session.try_send(received.value->kind, received.value->data);
            if (sent.disposition == httpserver::websocket::send_disposition::accepted) break;
            if (sent.disposition != httpserver::websocket::send_disposition::backpressured
                    || !(co_await session.writable()).ok()) {
                sending = false;
                break;
            }
        }
    }
    // Keep the owning session alive through close notification, even on
    // send failure. Production hosts choose their own close deadline.
    co_await closed.wait_for(std::chrono::seconds(5));
}
}  // namespace

int main(int argc, char** argv) {
    namespace srv = httpserver::server;
    namespace http = httpserver::http;
    srv::server_options options;
    options.concurrency().workers = 2;
    options.timeouts().ws_close = std::chrono::seconds(3);
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    if (!server.route(http::method::known(http::method_id::get), "/echo", echo).ok()) return 1;
    return v3_example::run(server, argc, argv);
}
