/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <httpserver.hpp>
#include <httpserver/server/tls_credentials.hpp>
#include "./v3_host_poll.hpp"

namespace srv = httpserver::server;
namespace http = httpserver::http;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string pem(const std::string& directory, const std::string& name) {
    std::ifstream input(directory + "/" + name);
    require(static_cast<bool>(input), "missing certificate fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
srv::tls_host_credentials host(const std::string& directory, const std::string& name) {
    return {name + ".example", pem(directory, name + ".pem") + pem(directory, "intermediate.pem"),
            pem(directory, name + "-key.pem"), {"http/1.1"}};
}
srv::server_options options(srv::loop_mode loop, std::shared_ptr<srv::tls_credentials> credentials) {
    srv::server_options value;
    value.loop() = loop;
    value.concurrency().workers = 2;
    value.timeouts().handshake = 500ms;
    value.timeouts().header = 2s;
    value.budgets().set(srv::resource::connections, 4);
    value.tls().provider = srv::tls_provider::system_default;
    value.tls().profile = srv::tls_profile::certificates;
    value.add_listener({"127.0.0.1", 0, true, std::move(credentials)});
    return value;
}
void unbound(srv::server_options value, http::outcome_code expected) {
    srv::native_server server(value);
    require(server.listen().code() == expected, "prebind outcome differs");
    require(!server.is_running(), "rejected server running");
    for (std::size_t index = 0; index < value.listener_count(); ++index) {
        require(server.get_bound_port(index) == 0, "rejected endpoint bound");
    }
}
void configuration(const std::string& directory, srv::loop_mode loop, bool enabled) {
    auto store = std::make_shared<srv::tls_credentials>();
    const srv::tls_credentials_config good{{host(directory, "a"), host(directory, "b")}, 0};
    const auto absent = enabled ? http::outcome_code::invalid_argument : http::outcome_code::not_supported;
    unbound(options(loop, nullptr), absent);
    unbound(options(loop, store), absent);
    auto invalid_options = options(loop, store);
    invalid_options.concurrency().workers = srv::max_workers + 1;
    unbound(invalid_options, http::outcome_code::invalid_argument);
    require(store->replace(good).code() == (enabled ? http::outcome_code::ok : http::outcome_code::not_supported), "credential installation");
    for (unsigned case_id = 0; case_id < 7; ++case_id) {
        auto invalid = good;
        if (case_id == 0) invalid.hosts.clear();
        if (case_id == 1) invalid.default_host = 2;
        if (case_id == 2) invalid.hosts[0].host = "bad host";
        if (case_id == 3) invalid.hosts[0].private_key_pem = invalid.hosts[1].private_key_pem;
        if (case_id == 4) invalid.hosts[0].certificate_chain_pem = "invalid certificate";
        if (case_id == 5) invalid.hosts[1].host = "A.EXAMPLE.";
        if (case_id == 6) invalid.hosts[0].alpn = {"h2"};
        require(store->replace(invalid).code() == absent, "invalid publication outcome");
    }
    if (enabled) {
        auto copy = *store;
        srv::native_server still_valid(options(loop, std::make_shared<srv::tls_credentials>(copy)));
        require(still_valid.listen().ok(), "rejected publication lost old generation");
    }
    for (auto protocol : {http::protocol::http_2, http::protocol::http_3}) {
        auto unsupported = options(loop, store);
        unsupported.protocols().enable(protocol);
        unbound(unsupported, http::outcome_code::not_supported);
    }
    auto profile = options(loop, store);
    profile.tls().profile = srv::tls_profile::mutual_tls;
    unbound(profile, enabled ? http::outcome_code::invalid_argument : http::outcome_code::not_supported);
    std::cerr << "PUBLIC_TLS_CONFIGURATION " << (enabled ? 15 : 14) << "\n";
}
int main(int argc, char** argv) {
    try {
        require(argc >= 5, "arguments: tls loop fixtures scenario [ports]");
        const bool enabled = std::string(argv[1]) == "yes";
        const auto loop = std::string(argv[2]) == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        const std::string directory(argv[3]), scenario(argv[4]);
        configuration(directory, loop, enabled);
        if (!enabled) return 0;
        auto store = std::make_shared<srv::tls_credentials>();
        auto second = std::make_shared<srv::tls_credentials>();
        require(store->replace({{host(directory, "a"), host(directory, "b")}, 0}).ok(), "first store");
        require(second->replace({{host(directory, "b")}, 0}).ok(), "second store");
        auto configured = options(loop, store);
        configured.add_listener({"127.0.0.1", 0, true, second});
        if (scenario == "mixed") configured.add_listener({"127.0.0.1", 0, false});
        if (scenario == "disabled10") configured.protocols().disable(http::protocol::http_1_0);
        if (scenario == "disabled11") {
            configured.protocols().disable(http::protocol::http_1_1);
            auto first_host = host(directory, "a");
            first_host.alpn.clear();
            require(store->replace({{first_host}, 0}).ok(), "HTTP/1.0-only credentials");
            require(second->replace({{first_host}, 0}).ok(), "HTTP/1.0-only second credentials");
        }
        if (scenario == "rollback") {
            require(argc == 7, "rollback ports");
            configured = options(loop, store);
            // Rebuild explicit ports; there is no native handle in this application.
            srv::server_options rollback;
            rollback.loop() = loop;
            rollback.concurrency().workers = 2;
            rollback.tls() = configured.tls();
            rollback.add_listener({"127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[5])), true, store});
            rollback.add_listener({"127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[6])), true, second});
            unbound(rollback, http::outcome_code::connection_closed);
            std::cout << "ROLLBACK_OK" << std::endl;
            return 0;
        }
        if (scenario == "timer_budget") configured.budgets().set(srv::resource::timers, 1);
        if (scenario == "storage_budget") configured.budgets().set(srv::resource::body_buffer_bytes, 49152);
        srv::diagnostics channel([](const srv::diagnostic_event&) {});
        configured.diagnostic_channel() = channel;
        srv::native_server server(configured);
        srv::drain_ticket ticket;
        std::atomic<unsigned> accepted{0}, handled{0}, observed{0};
        std::atomic<bool> failed{false}, stopping{false};
        std::atomic<bool> handler_shutdown{false};
        auto accept_hook = server.hooks().add<srv::hook_phase::accept_decision>([&](srv::accept_decision_ctx& ctx) {
            if (ctx.peer.address.to_string() != "127.0.0.1" || !ctx.peer.port) failed = true;
            ++accepted;
            if (scenario == "accept_stop") server.request_stop();
            return srv::hook_action::pass();
        });
        auto received_hook = server.hooks().add<srv::hook_phase::request_received>([&](srv::request_received_ctx& ctx) {
            if ((!ctx.tls && scenario != "mixed") || (ctx.tls && ctx.tls->has_client_certificate)) failed = true;
            ++observed;
            return srv::hook_action::pass();
        });
        require(server.route(http::method::known(http::method_id::get), "/installed",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                ++handled;
                if (x.peer().address.to_string() != "127.0.0.1" || !x.peer().port
                    || (!x.tls() && scenario != "mixed")
                    || (x.tls() && (x.tls()->has_client_certificate || x.tls()->client_certificate_verified))) failed = true;
                const std::string body = "installed-v3-tls-ok\n";
                http::fields fields;
                fields.append("Content-Length", std::to_string(body.size()));
                require(x.start_response(http::status::from_code(200), fields).ok(), "response start");
                const auto* bytes = reinterpret_cast<const std::byte*>(body.data());
                co_await x.writer().write({bytes, body.size()});
                co_await x.writer().finish();
                if (scenario == "handler_stop") {
                    server.request_stop();
                    handler_shutdown.store(true, std::memory_order_release);
                }
                if (scenario == "handler_drain") {
                    require(server.begin_drain(1s, ticket).ok(), "handler drain");
                    handler_shutdown.store(true, std::memory_order_release);
                }
            }).ok(), "route");
        require(server.listen().ok(), "public TLS listen");
        // Options and caller handles can die: the server owns its own association.
        configured = {};
        second.reset();
        std::jthread host_loop;
        if (loop == srv::loop_mode::external) {
            host_loop = std::jthread([&] {
            auto* driver = server.readiness();
            unsigned turns = 0;
            while (!stopping.load()) {
                auto interests = driver->interests();
                auto events = v3_host::wait(interests, v3_host::clock_type::now() + 1s);
                if (!driver->dispatch(events, v3_host::clock_type::now()).ok()) failed = true;
                ++turns;
            }
            std::cerr << "PUBLIC_TLS_EXTERNAL " << turns << '\n';
            });
        }
        std::cout << server.get_bound_port(0) << ' ' << server.get_bound_port(1);
        if (scenario == "mixed") std::cout << ' ' << server.get_bound_port(2);
        std::cout << std::endl;
        for (std::string line; std::getline(std::cin, line);) {
            if (line == "stop") break;
            if (line == "stats") {
                std::cout << accepted << ' ' << handled << ' ' << observed << std::endl;
            } else if (line == "handler_shutdown") {
                // Publish the handler's return (and drain ticket) to this
                // coordinator before it can close the peer or request stop.
                if (!handler_shutdown.load(std::memory_order_acquire)) {
                    std::cout << "PENDING" << std::endl;
                } else if (scenario == "handler_stop") {
                    require(!server.is_running(), "handler stop did not stop admission");
                    std::cout << "STOPPED" << std::endl;
                } else {
                    srv::drain_result result;
                    require(ticket.wait(result).ok() && result.status == srv::drain_status::completed
                            && !result.remaining, "handler drain wait");
                    std::cout << "DRAINED" << std::endl;
                }
            } else if (line == "replace") {
                require(store->replace({{host(directory, "b")}, 0}).ok(), "replace");
                std::cout << "REPLACED" << std::endl;
            } else if (line == "churn") {
                std::cout << "CHURNING" << std::endl;
                std::jthread writer([target = store, directory] {
                    for (unsigned index = 0; index < 12; ++index) {
                        require(target->replace({{host(directory, index % 2 ? "a" : "b")}, 0}).ok(), "concurrent publication");
                    }
                });
                writer.join();
                std::cout << "CHURNED" << std::endl;
            } else if (line == "invalid") {
                require(store->replace({}).code() == http::outcome_code::invalid_argument, "invalid replace");
                std::cout << "PRESERVED" << std::endl;
            } else if (line == "destroy") {
                store.reset();
                std::cout << "DESTROYED" << std::endl;
            } else if (line == "deny") {
                server.peer_policy().set_mode(srv::peer_policy_mode::reject_all);
                std::cout << "DENIED" << std::endl;
            } else if (line == "drain") {
                require(server.begin_drain(1s, ticket).ok(), "drain begin");
                srv::drain_result result;
                require(ticket.wait(result).ok() && result.status == srv::drain_status::completed && !result.remaining, "drain wait");
                std::cout << "DRAINED" << std::endl;
            } else {
                throw std::runtime_error("unknown command");
            }
        }
        stopping = true;
        server.request_stop();
        if (host_loop.joinable()) host_loop.join();
        server.stop();
        require(!failed.load(), "metadata/readiness mismatch");
        const auto counters = channel.counters();
        require(!counters.active_connections && !counters.active_streams
                && !counters.charged_response_queue_bytes, "admission/storage remained after stop");
        std::cerr << "PUBLIC_TLS_COUNTS " << accepted << ' ' << handled << ' ' << observed << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
