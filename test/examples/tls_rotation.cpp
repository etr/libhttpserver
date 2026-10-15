/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <atomic>
#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <httpserver.hpp>
#include <httpserver/features.hpp>

#include "./host_poll.hpp"
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""h;
namespace srv = httpserver::server;
namespace http = httpserver::http;
namespace {
int tls_off() {
    namespace srv = httpserver::server;
    namespace http = httpserver::http;
    srv::tls_credentials store;
    std::shared_ptr<srv::tls_psk_runtime> runtime;
    const auto replaced = store.replace({});
    const auto created = srv::tls_psk_runtime::create({1, 1}, runtime);
    const auto published = store.publish_acme({});
    const auto removed = store.remove_acme("a.example");
    srv::server_options options;
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;

    options.add_listener({"127.0.0.1", 0, true, {}, srv::listener_transport::tcp});
    srv::native_server server(options);
    const auto listened = server.listen();
    const auto unsupported = [](const auto& result) { return result.code() == http::outcome_code::not_supported; };
    if (!unsupported(replaced) || !unsupported(created) || !unsupported(published) || !unsupported(removed) ||
        !unsupported(listened) || server.get_bound_port(0) != 0) {
        return 1;
    }
    std::cout << "replace=not_supported runtime=not_supported publish=not_supported remove=not_supported "
                 "listen=not_supported port=0\n";
    return 0;
}
std::string pem(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("certificate input unavailable");
    }
    return {std::istreambuf_iterator<char>(input), {}};
}
srv::tls_credentials_config credentials(const std::string& directory, const std::string& name) {
    return {{{name + ".example",
              pem(directory + "/" + name + ".pem") + pem(directory + "/intermediate.pem"),
              pem(directory + "/" + name + "-key.pem"),
              {"http/1.1"}}},
            0};
}
void require(bool valid) {
    if (!valid) {
        throw std::runtime_error("TLS rotation operation failed");
    }
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[2]) != "managed" && std::string_view(argv[2]) != "external") {
        std::cerr << "usage: loop mode must be managed or external\n";
        return 2;
    }
    if (!httpserver::query_features().tls_provider) {
        return tls_off();
    }
    if (argc != 4) {
        std::cout << "usage: tls_rotation certificate-directory managed|external port\n"
                     "stdin: replace, invalid, stop (clients may keep connections open across replacement)\n";
        return argc == 1 ? 0 : 1;
    }
    try {
        unsigned port = 0;
        const std::string_view argument = argv[3];
        const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), port);
        if (parsed.ec != std::errc() || parsed.ptr != argument.data() + argument.size() || port > 65535) {
            return 2;
        }
        const std::string directory = argv[1];
        auto store = std::make_shared<srv::tls_credentials>();
        require(store->replace(credentials(directory, "a")).ok());
        srv::server_options options;
        options.loop() = std::string(argv[2]) == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        options.concurrency().workers = 2;
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = srv::tls_profile::certificates;
        options.add_listener({"127.0.0.1", static_cast<std::uint16_t>(port), true, store});
        srv::native_server server(options);
        require(server
                    .route(http::method::known(http::method_id::get), "/installed",
                           [](httpserver::exchange& exchange) -> httpserver::task<void> {
                               const std::string body = "rotation-example\n";
                               http::fields fields;
                               fields.append("Content-Length", std::to_string(body.size()));
                               require(exchange.start_response(http::status::from_code(200), fields).ok());
                               co_await exchange.writer().write(
                                   {reinterpret_cast<const std::byte*>(body.data()), body.size()});
                               co_await exchange.writer().finish();
                           })
                    .ok());
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
            if (line == "invalid") {
                candidate.hosts[0].private_key_pem = "invalid";
            } else {
                require(line == "replace");
            }
            const auto result = store->replace(candidate);
            require(line == "invalid" ? result.code() == http::outcome_code::invalid_argument : result.ok());
            std::cout << (result.ok() ? "REPLACED" : "PRESERVED") << std::endl;
        }
        server.request_stop();
        stopping = true;
        if (poller.joinable()) {
            poller.join();
        }
        server.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
