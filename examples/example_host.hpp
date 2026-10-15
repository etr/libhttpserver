/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef EXAMPLES_EXAMPLE_HOST_HPP_
#define EXAMPLES_EXAMPLE_HOST_HPP_

#include <charconv>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>
#include <httpserver/server/server.hpp>

namespace v3_example {
// A bounded demonstration host: optional lifetime in seconds, default ten.
inline int run(httpserver::server::native_server& server, int argc, char** argv) {
    int seconds = 10;
    const auto usage = [] {
        std::cerr << "usage: optional lifetime is one integer in seconds (1..3600)\n";
        return 2;
    };
    if (argc > 2) return usage();
    if (argc == 2) {
        const std::string_view argument(argv[1]);
        const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), seconds);
        if (parsed.ec != std::errc() || parsed.ptr != argument.data() + argument.size()
                || seconds <= 0 || seconds > 3600) return usage();
    }
    auto listened = server.listen();
    if (!listened.ok()) {
        std::cerr << listened.message() << '\n';
        return 1;
    }
    std::cout << "PORT=" << server.get_bound_port(0) << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (server.is_running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Only the controlling thread joins workers.
    server.stop();
    return 0;
}
}  // namespace v3_example
#endif  // EXAMPLES_EXAMPLE_HOST_HPP_
