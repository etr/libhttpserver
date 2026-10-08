/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <iostream>
#include <span>
#include <string>
#include <utility>

#include <httpserver.hpp>
#include <httpserver/features.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/server.hpp>

namespace srv = httpserver::server;
namespace http = httpserver::http;

httpserver::task<void> installed_response(httpserver::exchange& exchange) {
    http::fields fields;
    fields.append("Content-Length", "16");
    fields.append("Content-Type", "text/plain");
    if (!exchange.start_response(http::status::from_code(200), fields).ok()) co_return;
    const std::string body = "installed-v3-ok\n";
    co_await exchange.writer().write(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(body.data()), body.size()));
    co_await exchange.writer().finish();
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const bool tls = std::string(argv[1]) == "yes";
    if (httpserver::query_features().tls_provider != tls) return 2;
    srv::listener_options listener;
    listener.address = "127.0.0.1";
    listener.port = 0;
    listener.tls = true;
    srv::server_options unsupported;
    unsupported.add_listener(listener);
    unsupported.tls().provider = srv::tls_provider::system_default;
    unsupported.tls().profile = srv::tls_profile::certificates;
    srv::native_server boundary(std::move(unsupported));
    if (boundary.listen().code() != http::outcome_code::not_supported ||
        boundary.get_bound_port(0) != 0 || boundary.is_running()) return 3;
    boundary.stop();
    listener.tls = false;
    srv::server_options options;
    options.add_listener(listener);
    srv::native_server server(std::move(options));
    http::method_set methods;
    methods.set(http::method_id::get);
    if (!server.route(methods, "/installed", installed_response).ok()) return 4;
    if (!server.listen().ok() || server.get_bound_port(0) == 0) return 5;
    std::cout << server.get_bound_port(0) << std::endl;
    std::string stop;
    if (!std::getline(std::cin, stop) || stop != "stop") return 6;
    server.request_stop();
    server.stop();
    return server.is_running() ? 7 : 0;
}
