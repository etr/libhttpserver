/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/features.hpp>
#include <httpserver/server/configuration.hpp>
#include <httpserver/server/server.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include "../../examples/v3_host_poll.hpp"

namespace srv = httpserver::server;
namespace http = httpserver::http;
std::string read_pem(const std::string& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    srv::server_options options;
    options.loop() = std::string(argv[2]) == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
    options.concurrency().workers = 4;
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    options.protocols().enable(http::protocol::http_2);
    options.protocols().enable(http::protocol::http_3);
    options.budgets().set(srv::resource::header_fields, 4096);
    options.budgets().set(srv::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
    auto& h2 = options.http2().emplace();
    h2.max_concurrent_requests = 192;
    h2.headers = {70000, 70000, 512};
    h2.decoder_table_bytes = 8192;
    h2.encoder_table_bytes = 16384;
    auto& h3 = options.http3().emplace();
    h3.max_concurrent_requests = 24;
    h3.headers = {12000, 20000, 256};
    h3.decoder_table_bytes = 8192;
    h3.encoder_table_bytes = 0;
    h3.max_blocked_headers = 32;
    options.quic().emplace().max_stream_records = 96;
    auto credentials = std::make_shared<srv::tls_credentials>();
    options.add_listener({"127.0.0.1", 0, true, credentials});
    options.add_listener({"127.0.0.1", 0, true, credentials, srv::listener_transport::udp});
    srv::effective_protocol_limits resolved;
    auto result = options.resolve_limits(resolved);
    if (!httpserver::query_features().tls_provider) {
        srv::native_server server(options);
        srv::effective_protocol_limits sentinel;
        sentinel.quic.emplace().max_stream_records = 777;
        if (result.code() != http::outcome_code::not_supported || server.effective_limits(sentinel).code() != result.code() || sentinel.quic->max_stream_records != 777)
            return 3;
        std::cout << "TLS_OFF_CAPACITY_REJECTED" << std::endl;
        return 0;
    }
    if (!result.ok()) {
        std::cerr << "resolution failed\n";
        return 4;
    }
    srv::native_server server(options);
    srv::effective_protocol_limits inspected;
    if (!server.effective_limits(inspected).ok() || inspected.http2->max_concurrent_requests != 192 || inspected.http3->max_concurrent_requests != 24)
        return 5;
    const std::string directory = argv[1];
    if (!credentials->replace({{{"a.example", read_pem(directory + "/a.pem") + read_pem(directory + "/intermediate.pem"), read_pem(directory + "/a-key.pem"), {"h2", "h3"}}}, 0}).ok())
        return 6;
    std::atomic<unsigned> started{0};
    std::mutex mutex;
    std::vector<std::shared_ptr<httpserver::resume_signal>> held;
    server.route(http::method::known(http::method_id::get), "/hold", [&](httpserver::exchange& exchange) -> httpserver::task<void> {
        auto signal = std::make_shared<httpserver::resume_signal>();
        {
            std::lock_guard lock(mutex);
            held.push_back(signal);
        }
        ++started;
        co_await signal->wait();
        exchange.respond(http::status::from_code(204), {});
    });
    server.route(http::method::known(http::method_id::get), "/headers", [](httpserver::exchange& exchange) -> httpserver::task<void> {
        exchange.respond(http::status::from_code(204), {});
        co_return;
    });
    if (!server.listen().ok())
        return 7;
    std::atomic<bool> driving{true};
    std::thread host_loop;
    if (auto* driver = server.readiness()) {
        host_loop = std::thread([&, driver] {
            while (driving) {
                const auto snapshot = driver->interests();
                const auto events = v3_host::wait(snapshot, std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
                if (!driver->dispatch(events, std::chrono::steady_clock::now()).ok())
                    break;
            }
        });
    }
    std::cout << "PORT " << server.get_bound_port(0) << ' ' << server.get_bound_port(1) << std::endl;
    for (std::string command; std::getline(std::cin, command);) {
        if (command == "COUNTS") {
            std::cout << started.load() << std::endl;
        } else if (command == "RELEASE") {
            std::vector<std::shared_ptr<httpserver::resume_signal>> signals;
            {
                std::lock_guard lock(mutex);
                signals.swap(held);
            }
            for (auto& signal : signals)
                signal->signal();
            std::cout << "RELEASED" << std::endl;
        } else if (command == "STOP") {
            break;
        } else if (command == "INSPECT") {
            if (!server.effective_limits(inspected).ok())
                return 8;
            std::cout << inspected.http2->max_concurrent_requests << ' ' << inspected.http3->max_concurrent_requests << std::endl;
        }
    }
    driving = false;
    if (host_loop.joinable())
        host_loop.join();
    server.stop();
    return server.effective_limits(inspected).ok() && inspected.http2->max_concurrent_requests == 192 ? 0 : 9;
}
