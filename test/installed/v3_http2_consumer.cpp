/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <utility>
#include <vector>
#include <atomic>
#include <chrono>
#include <array>
#include <condition_variable>
#include <mutex>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <httpserver.hpp>
#include <httpserver/response_definition.hpp>
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
        if (argc != 4) return 2;
        const std::string directory = argv[1], loop = argv[2], alpn = argv[3];
        if (!httpserver::query_features().http2) {
            srv::server_options unavailable;
            unavailable.protocols().enable(http::protocol::http_2);
            unavailable.add_listener({"127.0.0.1", 0, false});
            srv::native_server server(unavailable);
            if (server.listen().code() != http::outcome_code::not_supported || server.get_bound_port(0)) throw std::runtime_error("TLS-off H2 admission");
            std::cout << "TLS_OFF_H2_REJECTED" << std::endl;
            return 0;
        }
        auto credentials = std::make_shared<srv::tls_credentials>();
        srv::tls_host_credentials host{"a.example", pem(directory + "/a.pem") + pem(directory + "/intermediate.pem"),
                                      pem(directory + "/a-key.pem"), {"http/1.1"}};
        if (alpn != "baseline" && alpn != "h1-bound") host.alpn = {"h2", "http/1.1"};
        if (alpn == "h2-only") host.alpn = {"h2"};
        if (alpn == "mtls") {
            host.profile = srv::tls_profile::mutual_tls;
            host.trust_roots_pem = pem(directory + "/client-root.pem");
        }
        if (alpn == "psk-reject") {
            std::shared_ptr<srv::tls_psk_runtime> runtime;
            if (!srv::tls_psk_runtime::create({1, 0}, runtime).ok()) throw std::runtime_error("PSK runtime");
            host.profile = srv::tls_profile::external_psk;
            host.psk = srv::tls_psk_config{[](auto, const auto&) { return srv::tls_psk_result{}; }, runtime};
            if (credentials->replace({{host}, 0}).code() != http::outcome_code::invalid_argument) throw std::runtime_error("PSK enabled on H2");
            std::cout << "PSK_H2_REJECTED" << std::endl;
            return 0;
        }
        auto published = credentials->replace({{host}, 0});
        if (!published.ok()) throw std::runtime_error("credential publication: " + std::to_string(static_cast<unsigned>(published.code())));
        srv::server_options options;
        options.loop() = loop == "external" ? srv::loop_mode::external : srv::loop_mode::managed;
        options.concurrency().workers = 4;
        if (alpn != "disabled" && alpn != "h1-bound") options.protocols().enable(http::protocol::http_2);
        if (alpn == "timeouts") {
            options.timeouts().header = std::chrono::milliseconds(500);
            options.timeouts().body_idle = std::chrono::milliseconds(500);
            options.timeouts().suspension = std::chrono::milliseconds(500);
            options.timeouts().write_idle = std::chrono::milliseconds(500);
            options.timeouts().ws_close = std::chrono::milliseconds(500);
        }
        if (alpn == "stream-limit") options.budgets().set(srv::resource::streams, 2);
        if (alpn == "connection-limit") options.budgets().set(srv::resource::connections, 1);
        if (alpn == "timer-limit") options.budgets().set(srv::resource::timers, 2);
        if (alpn == "header-limit") options.budgets().set(srv::resource::header_bytes, 4096);
        if (alpn == "body-limit") options.budgets().set(srv::resource::body_buffer_bytes, 196608);
        if (alpn == "output-limit") options.budgets().set(srv::resource::response_queue_bytes, 131072);
        options.tls().provider = srv::tls_provider::system_default;
        options.tls().profile = host.profile;
        options.add_listener({"127.0.0.1", 0, true, credentials});
        std::unique_ptr<srv::native_server> occupied;
        if (alpn == "rollback") {
            srv::server_options bound;
            bound.add_listener({"127.0.0.1", 0, false});
            occupied = std::make_unique<srv::native_server>(bound);
            if (!occupied->listen().ok()) throw std::runtime_error("rollback obstacle");
            options.add_listener({"127.0.0.1", occupied->get_bound_port(0), true, credentials});
        }
        if (alpn == "mixed") {
            options.add_listener({"127.0.0.1", 0, true, credentials});
            options.add_listener({"127.0.0.1", 0, false});
        }
        srv::diagnostics diagnostics([](const srv::diagnostic_event&) {});
        options.diagnostic_channel() = diagnostics;
        auto server_owner = std::make_unique<srv::native_server>(options);
        auto& server = *server_owner;
        srv::drain_ticket ticket;
        std::atomic<bool> ticket_live{false};
        std::atomic<unsigned> received_hooks{0}, completed_hooks{0};
        auto received_hook = server.hooks().add<srv::hook_phase::request_received>([&](srv::request_received_ctx& context) {
            if (!context.tls) throw std::runtime_error("missing hook TLS metadata");
            ++received_hooks; return srv::hook_action::pass();
        });
        auto completed_hook = server.hooks().add<srv::hook_phase::request_completed>([&](srv::request_completed_ctx&) {
            ++completed_hooks; return srv::hook_action::pass();
        });
        server.route(http::method::known(http::method_id::get), "/installed", [](httpserver::exchange& x) -> httpserver::task<void> {
            x.start_response(http::status::from_code(200), {});
            const std::string body = "installed-v3-h2-ok\n";
            co_await x.writer().write(std::as_bytes(std::span(body)));
            co_await x.writer().finish();
        });
        std::mutex state_mutex;
        std::condition_variable running_changed;
        bool running_released = false;
        httpserver::resume_signal suspended;
        std::atomic<unsigned> entries{0}, closes{0}, running_destroyed{0};
        server.route(http::method::known(http::method_id::post), "/body", [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++entries;
            x.admit_body({16384});
            std::string body;
            std::array<std::byte, 1024> buffer;
            for (;;) {
                auto part = co_await x.body().read_some(buffer);
                if (!part.status.ok()) co_return;
                if (part.end_of_body) break;
                body.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
            }
            http::fields fields;
            for (auto value : x.head().head_fields.all("x-order")) fields.append("x-order", value);
            x.start_response(http::status::from_code(200), fields);
            co_await x.writer().write(std::as_bytes(std::span(body)));
            http::fields trailers;
            for (auto value : x.body().trailers().all("x-trailer")) trailers.append("x-trailer", value);
            co_await x.writer().finish(trailers);
        });
        server.route_sync(http::method::known(http::method_id::post), "/sync",
            [](const http::request_head&, std::span<const std::byte> body) {
                srv::sync_response value;
                value.status = http::status::from_code(200);
                value.body.assign(body.begin(), body.end());
                return value;
            }, 8);
        server.route(http::method::known(http::method_id::post), "/reject", [](httpserver::exchange& x) -> httpserver::task<void> {
            x.respond(http::status::from_code(403), {}); co_return;
        });
        server.route(http::method::known(http::method_id::post), "/held-body", [&](httpserver::exchange& x) -> httpserver::task<void> {
            x.admit_body({16384});
            httpserver::resume_signal signal;
            x.suspend(signal);
            { std::lock_guard lock(state_mutex); suspended = signal; }
            ++entries;
            co_await signal.wait();
            if (x.disconnected()) co_return;
            std::array<std::byte, 1024> buffer;
            std::size_t received = 0;
            for (;;) {
                auto part = co_await x.body().read_some(buffer);
                if (!part.status.ok()) co_return;
                received += part.data.size();
                if (part.end_of_body) break;
            }
            http::fields fields; fields.append("x-consumed", std::to_string(received));
            x.respond(http::status::from_code(204), fields);
        });
        server.route(http::method::known(http::method_id::get), "/suspend", [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++entries;
            httpserver::resume_signal signal;
            x.suspend(signal);
            { std::lock_guard lock(state_mutex); suspended = signal; }
            co_await signal.wait();
            if (!x.disconnected()) x.respond(http::status::from_code(204), {});
        });
        server.route(http::method::known(http::method_id::get), "/resume", [&](httpserver::exchange& x) -> httpserver::task<void> {
            httpserver::resume_signal signal;
            { std::lock_guard lock(state_mutex); signal = suspended; }
            signal.signal();
            const auto resumed = co_await signal.wait();
            http::fields fields; fields.append("x-signal", std::to_string(static_cast<unsigned>(resumed)));
            x.respond(http::status::from_code(204), fields); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/running", [&](httpserver::exchange& x) -> httpserver::task<void> {
            struct frame_guard {
                std::atomic<unsigned>& destroyed;
                ~frame_guard() { ++destroyed; }
            } guard{running_destroyed};
            ++entries;
            std::unique_lock lock(state_mutex);
            if (!running_changed.wait_for(lock, std::chrono::seconds(5), [&] { return running_released; })) {
                throw std::runtime_error("running barrier timeout");
            }
            x.respond(http::status::from_code(204), {}); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/release-running", [&](httpserver::exchange& x) -> httpserver::task<void> {
            { std::lock_guard lock(state_mutex); running_released = true; }
            running_changed.notify_all(); x.respond(http::status::from_code(204), {}); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/large", [](httpserver::exchange& x) -> httpserver::task<void> {
            x.start_response(http::status::from_code(200), {});
            const std::string body(131072, 'L');
            co_await x.writer().write(std::as_bytes(std::span(body)));
            co_await x.writer().finish();
        });
        server.route(http::method::known(http::method_id::connect), "/ws", [&](httpserver::exchange& x) -> httpserver::task<void> {
            auto upgraded = co_await x.upgrade({});
            if (!upgraded.status.ok()) {
                x.respond(upgraded.rejection_status, upgraded.rejection_fields);
                co_return;
             }
            auto& session = *upgraded.session;
            httpserver::resume_signal closed_signal;
            session.on_close([&, closed_signal](auto) mutable { ++closes; closed_signal.signal(); });
            for (;;) {
                auto received = co_await session.receive();
                if (!received.status.ok() || !received.value) break;
                auto result = session.try_send(received.value->kind, received.value->data);
                while (result.disposition == httpserver::websocket::send_disposition::backpressured) {
                    if (!(co_await session.writable()).ok()) co_return;
                    result = session.try_send(received.value->kind, received.value->data);
                }
            }
            co_await closed_signal.wait();
        });
        server.route(http::method::known(http::method_id::get), "/meta", [](httpserver::exchange& x) -> httpserver::task<void> {
            if (!x.tls() || !x.peer().port || !x.connection_id()) throw std::runtime_error("missing native metadata");
            http::fields fields;
            fields.append("x-protocol", x.head().request_protocol == http::protocol::http_2 ? "h2" : "h1");
            fields.append("x-peer", x.peer().address.to_string());
            fields.append("x-client-verified", x.tls()->client_certificate_verified ? "yes" : "no");
            x.respond(http::status::from_code(204), fields); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/throw", [](httpserver::exchange&) -> httpserver::task<void> {
            throw std::runtime_error("contained application error"); co_return;
        });
        server.route(http::method::known(http::method_id::get), "/counts", [&](httpserver::exchange& x) -> httpserver::task<void> {
            const auto counters = diagnostics.counters();
            const std::string body = std::to_string(entries.load()) + " " + std::to_string(received_hooks.load()) + " " +
                std::to_string(completed_hooks.load()) + " " + std::to_string(closes.load()) + " " +
                std::to_string(counters.active_streams) + " " + std::to_string(running_destroyed.load());
            x.start_response(http::status::from_code(200), {});
            co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
        });
        httpserver::response_definition definition;
        const std::string definition_body = "reused-definition";
        http::fields base; base.append("x-base", "immutable");
        httpserver::response_definition::owned_bytes(http::status::from_code(200), base,
            {reinterpret_cast<const std::byte*>(definition_body.data()), reinterpret_cast<const std::byte*>(definition_body.data() + definition_body.size())}, definition);
        server.route(http::method::known(http::method_id::get), "/definition", [&](httpserver::exchange& x) -> httpserver::task<void> {
            httpserver::response_overlay overlay;
            for (auto value : x.head().head_fields.all("x-overlay")) overlay.headers.append("x-overlay", value);
            overlay.trailers.append("x-end", "definition");
            co_await httpserver::send_definition(x, definition, std::move(overlay));
        });
        httpserver::response_definition source_definition, failed_definition;
        httpserver::response_definition::factory(http::status::from_code(200), {}, [] {
            return httpserver::body_producer([step = 0]() mutable -> httpserver::body_chunk {
                static const std::string text = "source-data";
                if (!step++) return {http::outcome::okay(), std::as_bytes(std::span(text)), false};
                return {http::outcome::okay(), {}, true};
            });
        }, source_definition);
        httpserver::response_definition::factory(http::status::from_code(200), {}, [] {
            return httpserver::body_producer([]() -> httpserver::body_chunk {
                return {{http::outcome_code::connection_closed, "application source failed"}, {}, false};
            });
        }, failed_definition);
        server.route(http::method::known(http::method_id::get), "/source", [&](httpserver::exchange& x) -> httpserver::task<void> {
            co_await httpserver::send_definition(x, source_definition);
        });
        server.route(http::method::known(http::method_id::get), "/source-error", [&](httpserver::exchange& x) -> httpserver::task<void> {
            co_await httpserver::send_definition(x, failed_definition);
        });
        server.route(http::method::known(http::method_id::get), "/handler-drain", [&](httpserver::exchange& x) -> httpserver::task<void> {
            const auto begun = server.begin_drain(std::chrono::seconds(2), ticket);
            if (begun.ok()) ticket_live = true;
            srv::drain_result result;
            const auto waited = ticket.wait(result);
            http::fields fields;
            fields.append("x-begin", std::to_string(static_cast<unsigned>(begun.code())));
            fields.append("x-wait", std::to_string(static_cast<unsigned>(waited.code())));
            x.respond(http::status::from_code(204), fields);
            if (x.head().head_fields.first("x-finish")) {
                httpserver::resume_signal signal;
                { std::lock_guard lock(state_mutex); signal = suspended; }
                signal.signal();
            }
            std::cout << "HANDLER_DRAIN_RETURNED" << std::endl;
            co_return;
        });
        server.route(http::method::known(http::method_id::get), "/handler-stop", [&](httpserver::exchange&) -> httpserver::task<void> {
            server.request_stop();
            std::cout << "HANDLER_STOP_RETURNED" << std::endl;
            co_return;
        });
        const auto listened = server.listen();
        if (alpn == "rollback") {
            if (listened.ok() || server.is_running() || server.get_bound_port(0) || server.get_bound_port(1)) throw std::runtime_error("partial H2 bind was retained");
            std::cout << "ROLLBACK_H2_UNBOUND" << std::endl;
            return 0;
        }
        if (alpn == "disabled") {
            if (listened.code() != http::outcome_code::invalid_argument || server.get_bound_port(0)) throw std::runtime_error("disabled H2 policy accepted");
            std::cout << "DISABLED_H2_PREBIND" << std::endl;
            return 0;
        }
        if (!listened.ok()) throw std::runtime_error("listen: " + std::to_string(static_cast<unsigned>(listened.code())));
        std::atomic<bool> stopping{false};
        std::thread host_loop;
        std::mutex dispatch_mutex;
        auto* driver = server.readiness();
        const auto drive = [&, driver] {
                while (!stopping) {
                    const auto snapshot = driver->interests();
                    const auto events = v3_host::wait(snapshot);
                    if (stopping) break;
                    std::lock_guard lock(dispatch_mutex);
                    if (!driver->dispatch(events, std::chrono::steady_clock::now()).ok()) break;
                }
        };
        if (driver) host_loop = std::thread(drive);
        std::cout << server.get_bound_port(0) << std::endl;
        std::string command;
        std::vector<srv::readiness_event> saved_events;
        while (std::getline(std::cin, command) && command != "stop") {
            if (command == "stats") {
                auto counters = diagnostics.counters();
                std::cout << entries << ' ' << received_hooks << ' ' << completed_hooks << ' ' << closes << ' '
                          << counters.active_connections << ' ' << counters.active_streams << ' ' << counters.charged_response_queue_bytes << ' ' << running_destroyed << std::endl;
            } else if (command == "ports") {
                std::cout << server.get_bound_port(0) << ' ' << server.get_bound_port(1) << ' ' << server.get_bound_port(2) << std::endl;
            } else if (command == "snapshot") {
                if (auto* driver = server.readiness()) {
                    for (const auto& interest : driver->interests().sockets) saved_events.push_back({interest.key, interest.generation, true, true, true, true});
                }
                std::cout << "SNAPSHOT " << saved_events.size() << std::endl;
            } else if (command == "stale") {
                if (driver) {
                    std::lock_guard lock(dispatch_mutex);
                    const auto live = driver->interests();
                    std::vector<srv::readiness_event> retired;
                    for (const auto& event : saved_events) {
                        const auto current = [&](const auto& interest) { return interest.key == event.key && interest.generation == event.generation; };
                        if (std::none_of(live.sockets.begin(), live.sockets.end(), current)) {
                            retired.push_back(event); retired.push_back(event);
                        }
                    }
                    if (retired.empty()) throw std::runtime_error("no retired connection generation captured");
                    const auto before = received_hooks.load();
                    if (!driver->dispatch(retired, std::chrono::steady_clock::now()).ok() || received_hooks.load() != before)
                        throw std::runtime_error("stale readiness started H2 work");
                    std::cerr << "STALE_EVENTS_IGNORED " << retired.size() << std::endl;
                }
                std::cout << "STALE_GENERATIONS_INERT" << std::endl;
            } else if (command == "drain") {
                auto outcome = server.begin_drain(std::chrono::milliseconds(500), ticket);
                if (outcome.ok()) ticket_live = true;
                std::cout << "DRAIN " << static_cast<unsigned>(outcome.code()) << std::endl;
            } else if (command == "wait") {
                srv::drain_result result;
                auto outcome = ticket.wait(result);
                std::cout << "WAIT " << static_cast<unsigned>(outcome.code()) << ' ' << static_cast<unsigned>(result.status) << ' ' << result.remaining << std::endl;
            } else if (command == "rotate") {
                auto replacement = host;
                replacement.alpn = {"h2"};
                std::cout << "ROTATE " << static_cast<unsigned>(credentials->replace({{replacement}, 0}).code()) << std::endl;
            } else if (command == "deny" || command == "allow") {
                server.peer_policy().set_mode(command == "deny" ? srv::peer_policy_mode::reject_all : srv::peer_policy_mode::accept_all);
                std::cout << "POLICY " << command << std::endl;
            } else if (command == "release") {
                { std::lock_guard lock(state_mutex); running_released = true; }
                running_changed.notify_all();
                std::cout << "RELEASED" << std::endl;
            }
        }
        auto stale = std::move(saved_events);
        if (auto* driver = server.readiness()) {
            for (const auto& interest : driver->interests().sockets) stale.push_back({interest.key, interest.generation, true, true, true, true});
        }
        stopping = true;
        server.request_stop();
        if (host_loop.joinable()) host_loop.join();
        server.stop();
        if (auto* driver = server.readiness()) {
            const auto before = received_hooks.load();
            if (driver->dispatch(stale, std::chrono::steady_clock::now()).code() != http::outcome_code::invalid_state || received_hooks.load() != before)
                throw std::runtime_error("stopped readiness driver accepted work");
            std::cerr << "INACTIVE_DRIVER_REJECTED" << std::endl;
        }
        server_owner.reset();
        if (ticket_live) {
            srv::drain_result result;
            const auto outcome = ticket.wait(result);
            if (!outcome.ok()) throw std::runtime_error("retained ticket wait failed");
            std::cerr << "RETAINED_TICKET " << static_cast<unsigned>(result.status) << ' ' << result.remaining << std::endl;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
