/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <httpserver.hpp>
#include <httpserver/features.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include "v3_host_poll.hpp"

namespace srv = httpserver::server;
namespace http = httpserver::http;

int main(int argc, char** argv) {
    if (!httpserver::query_features().tls_provider) {
        std::cout << "TLS_OFF_H3_REJECTED" << std::endl; return 0;
    }
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: v3_http3 CHAIN_PEM KEY_PEM [managed|external]\n"; return 2;
    }
    const auto read = [](const char* path) {
        std::ifstream input(path); return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto credentials = std::make_shared<srv::tls_credentials>();
    if (!credentials->replace({{{"a.example", read(argv[1]), read(argv[2]), {"h3", "h2", "http/1.1"}}}, 0}).ok()) return 1;
    srv::server_options options;
    options.concurrency().workers = 2;
    options.loop() = argc == 4 && std::string_view(argv[3]) == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
    options.protocols().enable(http::protocol::http_2);
    options.protocols().enable(http::protocol::http_3);
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    options.add_listener({"127.0.0.1", 0, true, credentials, srv::listener_transport::udp});
    options.add_listener({"127.0.0.1", 0, true, credentials});
    srv::native_server server(options);
    if (!server.route(http::method::known(http::method_id::get), "/hello", [](httpserver::exchange& x) -> httpserver::task<void> {
        x.start_response(http::status::from_code(200), {});
        const std::string body = "hello native h3\n";
        co_await x.writer().write(std::as_bytes(std::span(body)));
        http::fields trailers; trailers.append("x-end", "native-fin");
        co_await x.writer().finish(trailers);
    }).ok()) return 1;
    if (!server.route(http::method::known(http::method_id::connect), "/ws/echo", [](httpserver::exchange& x) -> httpserver::task<void> {
        httpserver::ws_upgrade_options upgrade;
        upgrade.subprotocols = {"harness.v1"}; upgrade.require_subprotocol = true;
        upgrade.limits = {8192, 16384, 16384, 4, 4};
        auto opened = co_await x.upgrade(upgrade);
        if (!opened.status.ok()) { x.respond(opened.rejection_status, opened.rejection_fields); co_return; }
        auto session = std::move(*opened.session);
        httpserver::resume_signal closed;
        session.on_close([&](auto) { closed.signal(); });
        for (;;) {
            const auto received = co_await session.receive();
            if (!received.status.ok() || !received.value) { co_await closed.wait(); co_return; }
            for (;;) {
                const auto sent = session.try_send(received.value->kind, received.value->data);
                if (sent.disposition == httpserver::websocket::send_disposition::accepted) break;
                if (!sent.status.ok() || !(co_await session.writable()).ok()) co_return;
            }
        }
    }).ok() || !server.listen().ok()) return 1;
    std::cout << server.get_bound_port(0) << std::endl;
    std::cout << "TCP_PORT " << server.get_bound_port(1) << std::endl;
    std::atomic<bool> stopping{false};
    auto* driver = server.readiness();
    std::thread host;
    if (driver) host = std::thread([&] {
        while (!stopping.load()) {
            const auto snapshot = driver->interests();
            const auto events = v3_host::wait(snapshot, std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
            if (!driver->dispatch(events, std::chrono::steady_clock::now()).ok()) break;
        }
    });
    std::string command;
    while (std::getline(std::cin, command) && command != "stop") {}
    server.request_stop(); stopping = true;
    if (host.joinable()) host.join();
    server.stop();
}
