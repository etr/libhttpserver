/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <httpserver.hpp>
#include <httpserver/features.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include "./host_poll.hpp"
namespace srv = httpserver::server;
namespace http = httpserver::http;

std::string pem(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("missing certificate");
    return {std::istreambuf_iterator<char>(input), {}};
}

int main(int argc, char** argv) {
    try {
        if (argc != 7) return 2;
        const std::string fixtures = argv[1], loop = argv[2], protocol = argv[3], run = argv[4];
        const auto workers = std::stoul(argv[5]), payload = std::stoul(argv[6]);
        if ((loop != "managed" && loop != "external") || run.size() != 32 || !workers || workers > 32 || !payload || payload > 16384) return 2;
        const bool encrypted = protocol != "h1", h3 = protocol == "h3", h2 = protocol == "h2";
        if (protocol != "h1" && protocol != "h1-tls" && !h2 && !h3) return 2;
        srv::server_options options;
        options.loop() = loop == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        options.concurrency().workers = workers;
        options.protocols().disable(http::protocol::http_1_0);
        if (h2 || h3) options.protocols().disable(http::protocol::http_1_1);
        if (h2) options.protocols().enable(http::protocol::http_2);
        if (h3) options.protocols().enable(http::protocol::http_3);
        std::shared_ptr<srv::tls_credentials> credentials;
        if (encrypted && httpserver::query_features().tls_provider) {
            credentials = std::make_shared<srv::tls_credentials>();
            const auto alpn = h3 ? "h3" : h2 ? "h2" : "http/1.1";
            srv::tls_host_credentials host;
            host.host = "a.example";
            host.certificate_chain_pem = pem(fixtures + "/a.pem") + pem(fixtures + "/intermediate.pem");
            host.private_key_pem = pem(fixtures + "/a-key.pem");
            host.alpn = {alpn};
            if (!credentials->replace({{host}, 0}).ok()) throw std::runtime_error("credentials");
        }
        if (encrypted) {
            options.tls().provider = srv::tls_provider::system_default;
            options.tls().profile = srv::tls_profile::certificates;
        }
        options.add_listener({"127.0.0.1", 0, encrypted, credentials,
                              h3 ? srv::listener_transport::udp : srv::listener_transport::tcp});
        srv::native_server server(options);
        std::atomic<unsigned> warmup{0}, measured{0}, errors{0};
        const std::string body(payload, 'x');
        const auto routed = server.route(http::method::known(http::method_id::get), "/bench", [&](httpserver::exchange& x) -> httpserver::task<void> {
            const auto request_run = x.head().head_fields.first("x-run"), request_id = x.head().head_fields.first("x-request");
            const auto expected = h3 ? http::protocol::http_3 : h2 ? http::protocol::http_2 : http::protocol::http_1_1;
            if (!request_run || *request_run != run || !request_id || request_id->empty() || request_id->size() > 16 ||
                ((*request_id)[0] != 'w' && (*request_id)[0] != 'm') || x.head().request_protocol != expected) {
                ++errors; x.respond(http::status::from_code(400), {}); co_return;
            }
            http::fields fields;
            fields.append("x-run", run); fields.append("x-request", *request_id); fields.append("x-protocol", protocol);
            fields.append("content-length", std::to_string(payload));
            if (!x.start_response(http::status::from_code(200), fields).ok()) {
                ++errors; co_return;
            }
            const auto written = co_await x.writer().write(std::as_bytes(std::span(body)));
            if (!written.status.ok() || written.accepted != payload) {
                ++errors; co_return;
            }
            const auto finished = co_await x.writer().finish();
            if (!finished.status.ok()) {
                ++errors; co_return;
            }
            if ((*request_id)[0] == 'w') {
                ++warmup;
            } else {
                ++measured;
            }
        });
        if (!routed.ok()) throw std::runtime_error("route");
        const auto listened = server.listen();
        if (encrypted && !httpserver::query_features().tls_provider) {
            if (listened.code() != http::outcome_code::not_supported || server.get_bound_port(0)) throw std::runtime_error("TLS-off prebind rejection");
            std::cout << "UNAVAILABLE " << protocol << std::endl; return 0;
        }
        if (!listened.ok()) throw std::runtime_error("listen: " + std::to_string(static_cast<unsigned>(listened.code())));
        std::atomic<bool> stopping{false};
        std::thread host;
        struct cleanup {
            srv::native_server& server; std::atomic<bool>& stopping; std::thread& host;
            ~cleanup() {
                stopping = true;
                server.request_stop();
                if (host.joinable()) host.join();
                server.stop();
            }
        } guard{server, stopping, host};
        if (auto* driver = server.readiness()) {
            host = std::thread([&, driver] {
            try {
                while (!stopping) {
                    const auto events = v3_host::wait(driver->interests(), std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
                    if (!stopping && !driver->dispatch(events, std::chrono::steady_clock::now()).ok()) {
                        ++errors; break;
                    }
                }
            } catch (...) { ++errors; }
            });
        }
        std::cout << server.get_bound_port(0) << std::endl;
        std::string command;
        while (std::getline(std::cin, command) && command != "stop") {}
        stopping = true;
        server.request_stop();
        if (host.joinable()) host.join();
        server.stop();
        std::cout << "COUNTS {\"run_id\":\"" << run << "\",\"warmup\":" << warmup << ",\"measured\":" << measured << ",\"errors\":" << errors << "}" << std::endl;
        return errors ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
