/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <httpserver.hpp>
#include "./v3_host_poll.hpp"
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""h;
namespace srv = httpserver::server;
namespace http = httpserver::http;
namespace {
std::string pem(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("certificate input unavailable");
    return {std::istreambuf_iterator<char>(input), {}};
}
srv::tls_credentials_config credentials(const std::string& directory, const std::string& name) {
    return {{{name + ".example", pem(directory + "/" + name + ".pem") + pem(directory + "/intermediate.pem"),
              pem(directory + "/" + name + "-key.pem"), {"http/1.1"}}}, 0};
}
void require(bool valid) { if (!valid) throw std::runtime_error("TLS rotation operation failed"); }
}  // namespace
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cout << "usage: v3_tls_rotation certificate-directory managed|external port\n"
                     "stdin: replace, invalid, stop (clients may keep connections open across replacement)\n";
        return argc == 1 ? 0 : 1;
    }
    try {
        const std::string directory = argv[1];
        auto store = std::make_shared<srv::tls_credentials>();
        require(store->replace(credentials(directory, "a")).ok());
        srv::server_options options;
        options.loop() = std::string(argv[2]) == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        options.concurrency().workers = 2;
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = srv::tls_profile::certificates;
        options.add_listener({"127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[3])), true, store});
        srv::native_server server(options);
        require(server.route(http::method::known(http::method_id::get), "/installed", [](httpserver::exchange& exchange) -> httpserver::task<void> {
            const std::string body = "rotation-example\n";
            http::fields fields;
            fields.append("Content-Length", std::to_string(body.size()));
            require(exchange.start_response(http::status::from_code(200), fields).ok());
            co_await exchange.writer().write({reinterpret_cast<const std::byte*>(body.data()), body.size()});
            co_await exchange.writer().finish();
        }).ok());
        require(server.listen().ok());
        std::atomic<bool> stopping{false};
        std::jthread poller;
        if (options.loop() == srv::loop_mode::external) {
            poller = std::jthread([&] {
            auto* driver = server.readiness();
            while (!stopping) {
                auto interests = driver->interests();
                auto events = v3_host::wait(interests, v3_host::clock_type::now() + 50ms);
                require(driver->dispatch(events, v3_host::clock_type::now()).ok());
            }
            });
        }
        std::cout << server.get_bound_port(0) << std::endl;
        for (std::string line; std::getline(std::cin, line) && line != "stop";) {
            auto candidate = credentials(directory, "b");
            if (line == "invalid") candidate.hosts[0].private_key_pem = "invalid";
            else require(line == "replace");
            const auto result = store->replace(candidate);
            require(line == "invalid" ? result.code() == http::outcome_code::invalid_argument : result.ok());
            std::cout << (result.ok() ? "REPLACED" : "PRESERVED") << std::endl;
        }
        server.request_stop(); stopping = true;
        if (poller.joinable()) poller.join();
        server.stop();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
