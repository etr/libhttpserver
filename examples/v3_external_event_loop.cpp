/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <httpserver/server/server.hpp>
#include "./v3_host_poll.hpp"

int main(int argc, char** argv) {
    namespace srv = httpserver::server;
    namespace http = httpserver::http;
    // Optional seconds bounds a smoke invocation; ordinary hosting has
    // no synthetic periodic timer and waits only for library deadlines.
    std::optional<v3_host::clock_type::time_point> until;
    if (argc > 1) {
        const int seconds = std::atoi(argv[1]);
        if (seconds <= 0) return 2;
        until = v3_host::clock_type::now() + std::chrono::seconds(seconds);
    }
    srv::server_options options;
    options.loop() = srv::loop_mode::external;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    auto configured = server.route_sync(http::method::known(http::method_id::get), "/hello",
        [](const http::request_head&, std::span<const std::byte>) {
            srv::sync_response response;
            response.status = http::status::from_code(200);
            constexpr std::string_view body = "hello from an external loop\n";
            const auto* bytes = reinterpret_cast<const std::byte*>(body.data());
            response.body.assign(bytes, bytes + body.size());
            return response;
        }, 1024);
    if (!configured.ok()) return 1;
    const auto listened = server.listen();
    if (!listened.ok()) {
        std::cerr << listened.message() << '\n';
        return 1;
    }
    std::cout << "PORT=" << server.get_bound_port(0) << std::endl;
    int status = 0;
    try {
        auto* driver = server.readiness();
        auto snapshot = driver->interests();
        while (server.is_running()) {
            if (until && v3_host::clock_type::now() >= *until) break;
            auto events = v3_host::wait(snapshot, until);
            const auto result = driver->dispatch(events, v3_host::clock_type::now());
            if (!result.ok()) {
                std::cerr << result.message() << '\n';
                status = 1;
                break;
            }
            snapshot = driver->interests();
        }
        // poll registrations are local to wait(); none survive owner shutdown.
        snapshot = {};
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    server.stop();
    return status;
}
