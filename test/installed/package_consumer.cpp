/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <httpserver.hpp>
#include <httpserver/features.hpp>
#include <httpserver/server/server.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include "./host_poll.hpp"

namespace srv = httpserver::server;
namespace http = httpserver::http;

srv::server_options options(srv::loop_mode loop, std::string_view name = "plaintext") {
    srv::server_options result;
    result.loop() = loop;
    result.concurrency().workers = 2;
    result.timeouts().header = std::chrono::milliseconds(1000);
    result.budgets().set(srv::resource::connections, 16);
    result.add_listener({name == "invalid_listener" ? "invalid" : "127.0.0.1", 0, name == "tls_listener" || name == "http2"});
    return result;
}

void configuration_cases(srv::loop_mode loop, bool tls_enabled) {
    for (const std::string_view name : {"plaintext", "invalid_listener", "invalid_workers", "invalid_timeout",
                                      "invalid_budget", "provider_profile_mismatch", "tls_listener", "http2", "http3", "mixed_rejection"}) {
        auto configured = options(loop, name);
        if (name == "invalid_workers") configured.concurrency().workers = srv::max_workers + 1;
        if (name == "invalid_timeout") configured.timeouts().header = std::chrono::milliseconds(0);
        if (name == "invalid_budget") configured.budgets().set(srv::resource::connections, 0);
        if (name == "provider_profile_mismatch") configured.tls().provider = srv::tls_provider::system_default;
        const bool transport = name == "tls_listener" || name == "http2" || name == "http3" || name == "mixed_rejection";
        if (transport) {
            configured.tls().provider = srv::tls_provider::system_default;
            configured.tls().profile = srv::tls_profile::certificates;
            if (name == "http2") configured.protocols().enable(http::protocol::http_2);
            if (name == "http3") configured.protocols().enable(http::protocol::http_3);
            if (name == "mixed_rejection") {
                configured.add_listener({"127.0.0.1", 0, true});
                configured.protocols().enable(http::protocol::http_3);
            }
        }
        srv::native_server boundary(configured);
        const auto result = boundary.listen();
        const auto expected = name == "plaintext" ? http::outcome_code::ok :
            ((name == "tls_listener" || name == "http2") && !tls_enabled) ?
                http::outcome_code::not_supported : http::outcome_code::invalid_argument;
        if (result.code() != expected) throw std::runtime_error("configuration outcome: " + std::string(name));
        bool bound = false;
        for (std::size_t i = 0; i < configured.listener_count(); ++i) bound |= boundary.get_bound_port(i) != 0;
        if ((name == "plaintext") != bound || (name != "plaintext" && boundary.is_running())) {
            throw std::runtime_error("partial configuration binding");
        }
        std::cerr << "PACKAGE_CASE " << name << ' ' << (expected == http::outcome_code::ok ? "ok" :
            expected == http::outcome_code::not_supported ? "not_supported" : "invalid_argument") << ' ' << (bound ? "bound" : "unbound") << '\n';
        boundary.stop();
    }
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) return 1;
    const std::string_view tls_mode(argv[1]), loop_name(argv[2]);
    if ((tls_mode != "yes" && tls_mode != "no") || (loop_name != "managed" && loop_name != "external")) return 1;
    const auto features = httpserver::query_features();
    if (features.tls_provider != (tls_mode == "yes") || features.tcp_tls != (tls_mode == "yes") || features.http2 != (tls_mode == "yes") || features.http3 != (tls_mode == "yes")) return 2;
    std::cerr << "PACKAGE_PROVIDER " << features.provider_version << '\n';
    const auto loop = loop_name == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
    try {
        configuration_cases(loop, tls_mode == "yes");
        auto configured = options(loop);
        const std::string protocol = argc == 5 ? argv[4] : "h1";
        if (protocol != "h1") {
            const auto pem = [&](const std::string& name) {
                std::ifstream input(std::string(argv[3]) + "/" + name);
                if (!input) throw std::runtime_error("missing credentials");
                return std::string(std::istreambuf_iterator<char>(input), {});
            };
            auto credentials = std::make_shared<srv::tls_credentials>();
            srv::tls_host_credentials host{"a.example", pem("a.pem") + pem("intermediate.pem"),
                pem("a-key.pem"), {protocol == "tls" ? "http/1.1" : protocol}};
            const auto replaced = credentials->replace({{host}, 0});
            if (replaced.code() != (tls_mode == "yes" ? http::outcome_code::ok : http::outcome_code::not_supported)) return 8;
            configured = srv::server_options{};
            configured.loop() = loop;
            configured.concurrency().workers = 2;
            configured.tls().provider = srv::tls_provider::system_default;
            configured.tls().profile = srv::tls_profile::certificates;
            if (protocol == "h2") configured.protocols().enable(http::protocol::http_2);
            if (protocol == "h3") configured.protocols().enable(http::protocol::http_3);
            srv::listener_options listener{"127.0.0.1", 0, true, credentials};
            if (protocol == "h3") listener.transport = srv::listener_transport::udp;
            configured.add_listener(listener);
        }
        srv::native_server server(configured);
        const auto respond = [](const http::request_head&, std::span<const std::byte> body) {
            srv::sync_response response;
            response.status = http::status::from_code(200);
            const std::string text = body.empty() ? "installed-v3-ok\n" :
                std::string(reinterpret_cast<const char*>(body.data()), body.size());
            const auto* data = reinterpret_cast<const std::byte*>(text.data());
            response.body.assign(data, data + text.size());
            return response;
        };
        for (auto method : {http::method_id::get, http::method_id::post}) {
            if (!server.route_sync(http::method::known(method), "/installed", respond, 1024).ok()) return 4;
        }
        const auto listened = server.listen();
        if (protocol != "h1") {
            const bool expected = tls_mode == "yes" ? listened.ok() && server.get_bound_port(0) != 0 :
                listened.code() == http::outcome_code::not_supported && server.get_bound_port(0) == 0;
            if (!expected) return 9;
            std::cerr << "PACKAGE_SUPPORTED " << protocol << (tls_mode == "yes" ? " ok bound" : " not_supported unbound") << std::endl;
            if (tls_mode == "no") return 0;
        }
        if (!listened.ok() || server.get_bound_port(0) == 0) return 5;
        std::atomic<bool> stopping{false};
        std::atomic<bool> failed{false};
        std::jthread host;
        if (loop == srv::loop_mode::external) {
            host = std::jthread([&] {
                std::size_t dispatched = 0, registered = 0, wakes = 0;
                try {
                    auto* driver = server.readiness();
                    while (!stopping.load()) {
                        auto snapshot = driver->interests();
                        registered += snapshot.sockets.size();
                        wakes += snapshot.wake.has_value();
                        const auto events = v3_host::wait(snapshot, v3_host::clock_type::now() + std::chrono::seconds(5));
                        if (!driver->dispatch(events, v3_host::clock_type::now()).ok()) throw std::runtime_error("external dispatch");
                        ++dispatched;
                    }
                    if (!dispatched || !registered || !wakes) throw std::runtime_error("external host did no work");
                    std::cerr << "PACKAGE_EXTERNAL " << dispatched << ' ' << registered << ' ' << wakes << '\n';
                } catch (const std::exception& error) {
                    std::cerr << "CONSUMER_FAILURE_STOPPING " << stopping.load() << '\n';
                    std::cerr << error.what() << '\n';
                    failed.store(true);
                }
                server.stop();
            });
        }
        std::cout << server.get_bound_port(0) << std::endl;
        std::string stop;
        const bool requested = std::getline(std::cin, stop) && stop == "stop";
        stopping.store(true);
        server.request_stop();
        if (host.joinable()) host.join();
        else server.stop();
        return !requested || failed.load() || server.is_running() ? 6 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 7;
    }
}
