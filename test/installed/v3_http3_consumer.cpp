/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <httpserver.hpp>
#include <httpserver/features.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include "./v3_host_poll.hpp"
namespace srv = httpserver::server;
namespace http = httpserver::http;
std::string pem(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("missing certificate");
    return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4) return 2;
        const std::string directory = argv[1], loop = argv[2];
        const std::string mode = argc == 4 ? argv[3] : "basic";
        srv::server_options options;
        options.loop() = loop == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        options.concurrency().workers = 4;
        options.protocols().enable(http::protocol::http_3);
        if (!httpserver::query_features().tls_provider) {
            options.add_listener({"127.0.0.1", 0, false});
            srv::native_server server(options);
            if (server.listen().code() != http::outcome_code::not_supported || server.get_bound_port(0)) throw std::runtime_error("TLS-off H3 admission");
            std::cout << "TLS_OFF_H3_REJECTED" << std::endl;
            return 0;
        }
        if (mode == "listener-storage-limit") options.budgets().set(srv::resource::quic_reassembly_bytes, 90000);
        if (mode == "connections-one" || mode == "stalled") options.budgets().set(srv::resource::connections, 1);
        if (mode == "stalled") options.timeouts().handshake = std::chrono::milliseconds(250);
        if (mode == "timeouts") {
            options.timeouts().handshake = std::chrono::milliseconds(250);
            options.timeouts().header = std::chrono::milliseconds(300);
            options.timeouts().body_idle = std::chrono::milliseconds(300);
            options.timeouts().suspension = std::chrono::milliseconds(300);
            options.timeouts().write_idle = std::chrono::milliseconds(300);
        }
        auto credentials = std::make_shared<srv::tls_credentials>();
        srv::tls_host_credentials host{"a.example", pem(directory + "/a.pem") + pem(directory + "/intermediate.pem"),
            pem(directory + "/a-key.pem"), {"h3"}};
        if (mode == "wrong-store") host.alpn = {"http/1.1"};
        if (mode == "mixed") {
            host.alpn = {"http/1.1", "h2", "h3"}; options.protocols().enable(http::protocol::http_2);
        }
        if (mode == "psk") {
            std::shared_ptr<srv::tls_psk_runtime> runtime;
            if (!srv::tls_psk_runtime::create({1, 0}, runtime).ok()) throw std::runtime_error("PSK runtime");
            host.profile = srv::tls_profile::external_psk;
            host.certificate_chain_pem.clear(); host.private_key_pem.clear();
            host.psk = srv::tls_psk_config{[](auto, const auto&) { return srv::tls_psk_result{}; }, runtime};
            if (credentials->replace({{host}, 0}).code() != http::outcome_code::invalid_argument) throw std::runtime_error("H3 PSK publication");
            std::cout << "PSK_H3_REJECTED" << std::endl; return 0;
        }
        const auto published = credentials->replace({{host}, 0});
        if (!published.ok()) throw std::runtime_error("H3 credential publication refused: " + std::to_string(static_cast<unsigned>(published.code())));
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = srv::tls_profile::certificates;
        auto endpoint = srv::listener_options{"127.0.0.1", 0, true, credentials, srv::listener_transport::udp};
        if (mode == "no-udp") endpoint.transport = srv::listener_transport::tcp;
        if (mode == "no-tls") endpoint.tls = false;
        if (mode == "no-h3") options.protocols().disable(http::protocol::http_3);
        if (mode == "invalid-transport") endpoint.transport = static_cast<srv::listener_transport>(255);
        const auto probe = [&](bool udp, std::uint16_t port) {
            srv::server_options config;
            config.loop() = options.loop(); config.concurrency().workers = 2;
            if (udp) { config.protocols().enable(http::protocol::http_3); config.tls() = options.tls(); }
            config.add_listener({"127.0.0.1", port, udp, udp ? credentials : nullptr, udp ? srv::listener_transport::udp : srv::listener_transport::tcp});
            auto result = std::make_unique<srv::native_server>(config);
            if (!result->listen().ok()) throw std::runtime_error("bind probe failed");
            return result;
        };
        std::unique_ptr<srv::native_server> occupied;
        std::uint16_t rollback_port = 0;
        if (mode == "rollback" || mode == "rollback-reverse") {
            const bool first_udp = mode == "rollback-reverse";
            occupied = probe(!first_udp, 0);
            auto free = probe(first_udp, 0); rollback_port = free->get_bound_port(0); free->stop(); free.reset();
            options.add_listener({"127.0.0.1", rollback_port, first_udp, first_udp ? credentials : nullptr,
                first_udp ? srv::listener_transport::udp : srv::listener_transport::tcp});
            options.add_listener({"127.0.0.1", occupied->get_bound_port(0), !first_udp, !first_udp ? credentials : nullptr,
                first_udp ? srv::listener_transport::tcp : srv::listener_transport::udp});
        } else {
            if (mode == "mixed") { auto free = probe(false, 0); endpoint.port = free->get_bound_port(0); free->stop(); free.reset(); }
            options.add_listener(endpoint);
            if (mode == "mixed") options.add_listener({"127.0.0.1", endpoint.port, true, credentials});
        }
        srv::diagnostics diagnostics([](const srv::diagnostic_event&) {}); options.diagnostic_channel() = diagnostics;
        if (mode == "policy") options.peer_policy().mode = srv::peer_policy_mode::reject_all;
        std::atomic<unsigned> received{0}, completed{0}, running_entries{0}, cancelled_entries{0}, accepts{0}, rejected{0};
        std::mutex held_mutex;
        std::condition_variable held_changed;
        bool released = false;
        auto server_owner = std::make_unique<srv::native_server>(options);
        auto& server = *server_owner;
        auto accept = server.hooks().add<srv::hook_phase::accept_decision>([&](srv::accept_decision_ctx& context) { ++accepts; if (!context.accepted) ++rejected; return srv::hook_action::pass(); });
        auto seen = server.hooks().add<srv::hook_phase::request_received>([&](srv::request_received_ctx& context) {
            if (!context.tls) throw std::runtime_error("missing hook TLS");
            ++received; return srv::hook_action::pass();
        });
        auto done = server.hooks().add<srv::hook_phase::request_completed>([&](srv::request_completed_ctx&) {
            ++completed; return srv::hook_action::pass();
        });
        server.route(http::method::known(http::method_id::post), "/body", [](httpserver::exchange& x) -> httpserver::task<void> {
            x.admit_body({16384});
            std::array<std::byte, 1024> buffer;
            std::string body;
            for (;;) {
                auto part = co_await x.body().read_some(buffer);
                if (!part.status.ok()) co_return;
                body.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
                if (part.end_of_body) break;
            }
            x.start_response(http::status::from_code(200), {});
            co_await x.writer().write(std::as_bytes(std::span(body)));
            http::fields trailers;
            for (auto value : x.body().trailers().all("x-upload-end")) trailers.append("x-upload-end", value);
            co_await x.writer().finish(trailers);
        });
        server.route(http::method::known(http::method_id::get), "/meta", [](httpserver::exchange& x) -> httpserver::task<void> {
            if (!x.tls() || !x.peer().port || !x.connection_id() || x.head().request_protocol != http::protocol::http_3)
                throw std::runtime_error("missing native H3 metadata");
            http::fields fields;
            fields.append("x-protocol", "h3"); fields.append("x-peer", x.peer().address.to_string());
            x.respond(http::status::from_code(204), fields); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/running", [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++running_entries;
            { std::unique_lock lock(held_mutex); held_changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; }); }
            if (!x.disconnected()) x.respond(http::status::from_code(204), {});
            co_return;
        });
        server.route(http::method::known(http::method_id::get), "/release", [&](httpserver::exchange& x) -> httpserver::task<void> {
            { std::lock_guard lock(held_mutex); released = true; }
            held_changed.notify_all(); x.respond(http::status::from_code(204), {}); co_return;
        });
        server.route(http::method::known(http::method_id::post), "/cancel", [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++cancelled_entries; x.admit_body({16384});
            std::array<std::byte, 64> buffer;
            while (!(co_await x.body().read_some(buffer)).end_of_body && !x.disconnected()) {}
        });
        server.route(http::method::known(http::method_id::get), "/counts", [&](httpserver::exchange& x) -> httpserver::task<void> {
            const auto body = std::to_string(received.load()) + " " + std::to_string(completed.load()) + " " +
                std::to_string(running_entries.load()) + " " + std::to_string(cancelled_entries.load());
            x.start_response(http::status::from_code(200), {});
            co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
        });
        server.route(http::method::known(http::method_id::get), "/hello", [](httpserver::exchange& x) -> httpserver::task<void> {
            http::fields fields; fields.append("x-native-h3", "authenticated-route");
            x.start_response(http::status::from_code(200), fields);
            const std::string body = "hello native h3\n";
            co_await x.writer().write(std::as_bytes(std::span(body)));
            http::fields trailers; trailers.append("x-end", "native-fin");
            co_await x.writer().finish(trailers);
        });
        const auto listened = server.listen();
        if (mode == "listener-storage-limit") {
            if (listened.code() != http::outcome_code::limit_exceeded || server.get_bound_port(0) || accepts.load()) throw std::runtime_error("listener storage not reserved before bind");
            std::cout << "LIMIT_H3_UNBOUND" << std::endl; return 0;
        }
        if (mode == "rollback" || mode == "rollback-reverse") {
            if (listened.ok() || server.get_bound_port(0) || server.get_bound_port(1) || accepts.load()) throw std::runtime_error("partial H3 bind retained");
            auto reusable = probe(mode == "rollback-reverse", rollback_port); reusable->stop();
            std::cout << "ROLLBACK_H3_UNBOUND " << mode << std::endl; return 0;
        }
        if (mode == "no-udp" || mode == "no-tls" || mode == "no-h3" || mode == "invalid-transport" || mode == "wrong-store") {
            if (listened.code() != http::outcome_code::invalid_argument || server.get_bound_port(0) || accepts.load()) throw std::runtime_error("H3 prebind validation");
            std::cout << "PREBIND_H3_REJECTED " << mode << std::endl; return 0;
        }
        if (!listened.ok()) throw std::runtime_error("H3 listen refused: " + std::to_string(static_cast<unsigned>(listened.code())));
        if (mode == "mixed" && server.get_bound_port(0) != server.get_bound_port(1)) throw std::runtime_error("mixed TCP/UDP identity");
        std::cout << server.get_bound_port(0) << std::endl;
        std::atomic<bool> stopping{false};
        auto* driver = server.readiness();
        std::thread host_thread;
        if (driver) host_thread = std::thread([&] {
            while (!stopping.load()) {
                const auto snapshot = driver->interests();
                auto events = v3_host::wait(snapshot, std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
                if (!driver->dispatch(events, std::chrono::steady_clock::now()).ok()) break;
            }
        });
        std::string command;
        while (std::getline(std::cin, command) && command != "stop") {}
        { std::lock_guard lock(held_mutex); released = true; }
        held_changed.notify_all();
        if (mode == "destroy") {
            stopping = true; if (host_thread.joinable()) host_thread.join();
            server_owner.reset();
            if (received.load() != completed.load()) throw std::runtime_error("destruction missed stream completion");
            std::cerr << "H3_DESTROYED " << received.load() << " " << completed.load() << std::endl;
            return 0;
        }
        const auto stopping_at = std::chrono::steady_clock::now(); server.request_stop();
        if (std::chrono::steady_clock::now() - stopping_at > std::chrono::milliseconds(100)) throw std::runtime_error("request_stop blocked");
        stopping = true;
        if (host_thread.joinable()) host_thread.join();
        server.stop();
        if (driver) {
            if (!driver->interests().sockets.empty() || driver->interests().wake || driver->dispatch({}, std::chrono::steady_clock::now()).code() != http::outcome_code::invalid_state)
                throw std::runtime_error("inactive external driver");
            std::cerr << "INACTIVE_DRIVER_REJECTED" << std::endl;
        }
        if (mode == "policy") {
            const auto counters = diagnostics.counters();
            const auto tls = counters.events[static_cast<std::size_t>(srv::diagnostic_kind::tls_completed)];
            if (!rejected.load() || received.load() || tls) throw std::runtime_error("H3 policy admission hooks");
            std::cerr << "POLICY_REFUSAL " << rejected.load() << " " << received.load() << " " << tls << std::endl;
        }
        std::cerr << "H3_ACCEPT_TOTAL " << accepts.load() << " " << rejected.load() << std::endl;
        std::cerr << "H3_TLS_COMPLETED " << diagnostics.counters().events[static_cast<std::size_t>(srv::diagnostic_kind::tls_completed)] << std::endl;
        std::cerr << "H3_STOP " << received.load() << " " << completed.load() << std::endl;
        server_owner.reset();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl; return 1;
    }
}
