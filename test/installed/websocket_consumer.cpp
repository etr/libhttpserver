/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/server.hpp>
#include <httpserver/websocket/session.hpp>
#include "./host_poll.hpp"

namespace h = httpserver;
std::atomic<unsigned> closes{0}, blocked{0}, cancelled{0};
std::atomic<unsigned> receive_entered{0}, receive_completed{0}, receive_retired{0};
std::atomic<unsigned> receive_cancelled{0}, receive_outcome{0};
struct receive_retirement {
    bool observed;
    ~receive_retirement() { if (observed) ++receive_retired; }
};
h::task<void> echo(h::exchange& exchange) {
    h::ws_upgrade_options options;
    options.limits.max_message_bytes = 65536;
    options.limits.incoming_bytes = 65536;
    options.limits.output_bytes = 65546;
    options.limits.outgoing_messages = 1;
    auto upgraded = co_await exchange.upgrade(options);
    if (!upgraded.status.ok()) {
        upgraded.rejection_fields.append("Content-Length", "0");
        upgraded.rejection_fields.append("Connection", "close");
        exchange.respond(upgraded.rejection_status, upgraded.rejection_fields);
        co_return;
    }
    auto session = std::move(*upgraded.session);
    const bool observe_receive = exchange.head().route_path == "/cancel";
    receive_retirement retirement{observe_receive};
    h::resume_signal closed;
    auto notified = std::make_shared<std::atomic<bool>>(false);
    session.on_close([closed, notified](h::websocket::close_info) mutable {
        if (!notified->exchange(true)) ++closes;
        closed.signal();
    });
    if (exchange.head().route_path == "/pressure") {
        std::vector<std::byte> data(65536, std::byte{42});
        for (unsigned i = 0; i < 128; ++i) {
            for (;;) {
                auto sent = session.try_send(h::websocket::message_kind::binary, data);
                if (sent.disposition == h::websocket::send_disposition::accepted) break;
                if (sent.disposition != h::websocket::send_disposition::backpressured) co_return;
                ++blocked;
                if (!(co_await session.writable()).ok()) {
                    ++cancelled;
                    co_await closed.wait_for(std::chrono::seconds(3));
                    co_return;
                }
            }
        }
    } else {
        for (;;) {
            if (observe_receive) ++receive_entered;
            auto received = co_await session.receive();
            if (observe_receive) {
                receive_outcome = static_cast<unsigned>(received.status.code());
                ++receive_completed;
                // Transport EOF terminates the pending public receive with
                // connection_closed; distinguish it from a delivered message.
                if (!received.value && received.status.code() == h::http::outcome_code::connection_closed) ++receive_cancelled;
            }
            if (!received.value) break;
            auto sent = session.try_send(received.value->kind, received.value->data);
            if (sent.disposition != h::websocket::send_disposition::accepted) break;
        }
    }
    co_await closed.wait_for(std::chrono::seconds(3));
}

int main(int argc, char** argv) {
    namespace srv = h::server;
    if (argc != 2) return 1;
    srv::server_options options;
    options.loop() = std::string(argv[1]) == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
    options.concurrency().workers = 2;
    options.timeouts().ws_close = std::chrono::milliseconds(500);
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    for (const auto* route : {"/echo", "/cancel", "/pressure"}) {
        if (!server.route(h::http::method::known(h::http::method_id::get), route, echo).ok()) return 2;
    }
    if (!server.listen().ok() || !server.get_bound_port(0)) return 3;
    std::atomic<bool> stopping{false}, failed{false};
    std::jthread host;
    if (options.loop() == srv::loop_mode::external) {
        host = std::jthread([&] {
            auto* driver = server.readiness();
            while (!stopping) {
                auto snapshot = driver->interests();
                auto events = v3_host::wait(snapshot, v3_host::clock_type::now() + std::chrono::seconds(3));
                if (!driver->dispatch(events, v3_host::clock_type::now()).ok()) failed = true;
            }
        });
    }
    std::cout << server.get_bound_port(0) << std::endl;
    std::string command;
    while (std::getline(std::cin, command) && command != "stop") {
        if (command == "stats") std::cout << closes << ' ' << blocked << ' ' << cancelled << ' ' << receive_entered << ' ' << receive_completed << ' ' << receive_retired
            << ' ' << receive_cancelled << ' ' << receive_outcome << std::endl;
    }
    stopping = true;
    server.request_stop();
    if (host.joinable()) host.join();
    server.stop();
    std::cerr << "WS_COUNTS " << closes << ' ' << blocked << ' ' << cancelled << ' ' << receive_entered << ' ' << receive_completed << ' ' << receive_retired
            << ' ' << receive_cancelled << ' ' << receive_outcome << std::endl;
    return failed || server.is_running() ? 4 : 0;
}
