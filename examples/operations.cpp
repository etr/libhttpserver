/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "./example_host.hpp"
#include "./example_reply.hpp"
namespace srv = httpserver::server;
namespace http = httpserver::http;
int main(int argc, char**) {
    if (argc != 1) {
        std::cerr << "usage: operations; stdin: stats, deny-loopback, allow-loopback, stop\n";
        return 2;
    }
    std::atomic<unsigned> exceptions{0}, misses{0}, completed{0}, sent{0};
    unsigned dispatched = 0;
    srv::diagnostics channel([&](const srv::diagnostic_event&) { ++dispatched; });
    srv::server_options options;
    options.concurrency().workers = 2;
    options.timeouts().header = std::chrono::seconds(3);
    options.timeouts().write_idle = std::chrono::seconds(3);
    options.budgets().set(srv::resource::connections, 4);
    options.budgets().set(srv::resource::header_bytes, 8192);
    options.budgets().set(srv::resource::response_queue_bytes, 4096);
    options.diagnostic_channel() = channel;
    options.peer_policy().deny = {"192.0.2.0/24"};
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    std::vector<srv::hook_handle> hooks;
    hooks.push_back(server.hooks().add<srv::hook_phase::accept_decision>(
        [](srv::accept_decision_ctx&) { return srv::hook_action::pass(); }));
    hooks.push_back(server.hooks().add<srv::hook_phase::request_received>([](srv::request_received_ctx& ctx) {
        if (ctx.request.route_path != "/short") {
            return srv::hook_action::pass();
        }
        srv::hook_response response;
        response.status = http::status::from_code(202);
        return srv::hook_action::respond_with(std::move(response));
    }));
    hooks.push_back(server.hooks().add<srv::hook_phase::route_resolved>([&](srv::route_resolved_ctx& ctx) {
        if (!ctx.matched) {
            ++misses;
        }
        return srv::hook_action::pass();
    }));
    hooks.push_back(server.hooks().add<srv::hook_phase::before_handler>(
        [](srv::before_handler_ctx&) { return srv::hook_action::pass(); }));
    hooks.push_back(server.hooks().add<srv::hook_phase::handler_exception>([&](srv::handler_exception_ctx&) {
        ++exceptions;
        return srv::hook_action::pass();
    }));
    hooks.push_back(server.hooks().add<srv::hook_phase::after_handler>([](srv::after_handler_ctx& ctx) {
        ctx.fields.append("X-Hook", "after");
        return srv::hook_action::pass();
    }));
    hooks.push_back(server.hooks().add<srv::hook_phase::response_sent>([&](srv::response_sent_ctx& ctx) {
        if (ctx.status != 0) {
            ++sent;
        }
        return srv::hook_action::pass();
    }));
    hooks.push_back(server.hooks().add<srv::hook_phase::request_completed>([&](srv::request_completed_ctx& ctx) {
        if (ctx.succeeded && ctx.end.ok()) {
            ++completed;
        }
        return srv::hook_action::pass();
    }));
    if (!server
             .route(http::method::known(http::method_id::get), "/peer",
                    [](httpserver::exchange& x) -> httpserver::task<void> {
                        co_await v3_example::reply(x, "peer=" + x.peer().to_string() + "\n");
                    })
             .ok() ||
        !server
             .route(http::method::known(http::method_id::get), "/throw",
                    [](httpserver::exchange&) -> httpserver::task<void> {
                        throw std::runtime_error("private exception");
                        co_return;
                    })
             .ok() ||
        !server.listen().ok()) {
        return 1;
    }
    std::cout << "PORT=" << server.get_bound_port(0) << std::endl;
    for (std::string command; std::getline(std::cin, command) && command != "stop";) {
        if (command == "deny-loopback") {
            if (!server.peer_policy().deny("127.0.0.1").ok()) {
                break;
            }
            std::cout << "DENIED" << std::endl;
        } else if (command == "allow-loopback") {
            if (!server.peer_policy().remove_denied("127.0.0.1").ok()) {
                break;
            }
            std::cout << "ALLOWED" << std::endl;
        } else if (command == "stats") {
            channel.dispatch();
            const auto counters = channel.counters();
            std::cout << "exceptions=" << exceptions << " misses=" << misses << " completed=" << completed
                      << " sent=" << sent << " delivered=" << dispatched << " active=" << counters.active_connections
                      << " streams=" << counters.active_streams << " queued=" << counters.charged_response_queue_bytes
                      << std::endl;
        } else {
            std::cout << "UNKNOWN_COMMAND" << std::endl;
        }
    }
    server.stop();
    channel.dispatch();
}
