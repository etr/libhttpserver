/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cerrno>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/detail/tls_session.hpp>
#include <httpserver/exchange.hpp>
#include "../unit/http2_request_fixture.hpp"
#include "../unit/tls_credentials_fixture.hpp"
namespace {
namespace h = httpserver;
namespace hd = h::detail;
using clock_type = std::chrono::steady_clock;
struct counters {
    unsigned started = 0, completed = 0, cancelled = 0;
    std::size_t uploaded = 0, data = 0;
};
struct lifetime {
    counters& counts;
    h::stop_token token;
    ~lifetime() { if (token.stop_requested()) ++counts.cancelled; }
};
struct connection {
    int fd;
    counters& counts;
    h::resume_signal& hold;
    h::resume_signal& upload;
    h::server::resource_budget budget = h2test::budget();
    h::server::route_registry routes;
    h::manual_executor executor;
    hd::tls_session tls;
    std::vector<std::unique_ptr<h::websocket::session>> sessions;
    std::unique_ptr<hd::http2_request_engine> engine;
    std::vector<std::byte> ciphertext;
    std::vector<std::uint8_t> inspect;
    std::size_t sent = 0, total = 0, magic = 24;
    bool connected = false, terminal = false;
    clock_type::time_point deadline = clock_type::now() + std::chrono::seconds(15);
    connection(int socket, hd::tls_credentials_selection selection, counters& c, h::resume_signal& a, h::resume_signal& b)
        : fd(socket), counts(c), hold(a), upload(b), tls(std::move(selection), true) {
        if (!h::server::route_registry::create(budget, routes).ok()) throw std::runtime_error("routes refused");
        for (auto method : {h::http::method_id::get, h::http::method_id::post}) {
            routes.route(h::http::method::known(method), "/", [](h::exchange& x) -> h::task<void> {
                x.admit_body({1048576}); auto body = co_await x.body().collect(1048576);
                if (!body.status.ok()) co_return;
                x.start_response(h::http::status::from_code(200), {});
                const std::string bytes = "http2 fixture";
                co_await x.writer().write(std::as_bytes(std::span(bytes))); co_await x.writer().finish();
            });
        }
        routes.route(h::http::method::known(h::http::method_id::get), "/health", [](h::exchange& x) -> h::task<void> {
            x.respond(h::http::status::from_code(204), {}); co_return;
        });
        routes.route(h::http::method::known(h::http::method_id::get), "/hold", [this](h::exchange& x) -> h::task<void> {
            ++counts.started; lifetime guard{counts, x.cancellation()}; co_await hold.wait();
            x.respond(h::http::status::from_code(200), {}); ++counts.completed;
        });
        routes.route(h::http::method::known(h::http::method_id::post), "/upload-hold", [this](h::exchange& x) -> h::task<void> {
            ++counts.started; lifetime guard{counts, x.cancellation()};
            x.admit_body({1048576}); co_await upload.wait();
            std::array<std::byte, 4096> buffer{};
            for (;;) {
                auto read = co_await x.body().read_some(buffer);
                if (!read.status.ok()) co_return;
                counts.uploaded += read.data.size();
                if (read.end_of_body) break;
            }
            x.respond(h::http::status::from_code(200), {}); ++counts.completed;
        });
        routes.route(h::http::method::known(h::http::method_id::get), "/large", [](h::exchange& x) -> h::task<void> {
            x.start_response(h::http::status::from_code(200), {});
            const std::string bytes(16384, 'L');
            for (unsigned i = 0; i < 8; ++i) co_await x.writer().write(std::as_bytes(std::span(bytes)));
            co_await x.writer().finish();
        });
        routes.route(h::http::method::known(h::http::method_id::connect), "/ws", [this](h::exchange& x) -> h::task<void> {
            auto upgraded = co_await x.upgrade({});
            if (!upgraded.status.ok()) {
                x.respond(upgraded.rejection_status, {}); co_return;
            }
            if (sessions.size() >= 16) throw std::runtime_error("WS fixture limit");
            auto retained = std::make_unique<h::websocket::session>(std::move(*upgraded.session));
            auto& session = *retained;
            sessions.push_back(std::move(retained));
            for (;;) {
                auto received = co_await session.receive();
                if (!received.status.ok()) co_return;
                if (!received.value) co_return;
                auto& message = *received.value;
                auto sent = session.try_send(message.kind, message.data);
                if (sent.disposition != h::websocket::send_disposition::accepted) throw std::runtime_error("WS echo refused");
            }
        });
        hd::http2_request_limits limits;
        limits.max_streams = 16;
        engine = std::make_unique<hd::http2_request_engine>(budget, routes, executor, limits);
    }
    ~connection() { engine.reset(); executor.run_pending(); close(fd); }
    void observe(std::span<const std::uint8_t> bytes) {
        auto skip = std::min(magic, bytes.size()); magic -= skip; bytes = bytes.subspan(skip);
        inspect.insert(inspect.end(), bytes.begin(), bytes.end());
        while (inspect.size() >= 9) {
            auto n = (std::size_t{inspect[0]} << 16) | (std::size_t{inspect[1]} << 8) | inspect[2];
            if (n > 65536) {
                inspect.clear(); return;
            }
            if (inspect.size() < n + 9) break;
            if (inspect[3] == 0) counts.data += n;
            inspect.erase(inspect.begin(), inspect.begin() + n + 9);
        }
    }
    bool step(std::int16_t events) {
        if (clock_type::now() > deadline || total > 8 * 1024 * 1024) return false;
        std::array<std::byte, 16384> buffer{};
        if (events & POLLIN) {
            auto n = recv(fd, buffer.data(), std::min(buffer.size(), tls.input_capacity()), 0);
            if (n == 0) return false;
            if (n > 0) {
                total += n;
                if (!tls.feed(std::span(buffer).first(n))) return false;
            } else if (errno != EAGAIN && errno != EINTR) {
                return false;
            }
        }
        for (unsigned pump = 0; pump < 64; ++pump) {
            if (!connected) {
                auto result = tls.handshake();
                if (result.state == hd::tls_session::progress::complete) {
                    if (tls.negotiated_protocol() != hd::tls_negotiated_protocol::h2) throw std::runtime_error("ALPN h2 required");
                    connected = true;
                } else if (result.state == hd::tls_session::progress::failed) {
                    return false;
                }
            }
            bool progress = false;
            if (connected && !terminal) {
                auto read = tls.read(buffer);
                if (read.state == hd::tls_session::progress::complete && read.bytes) {
                    auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(buffer.data()), read.bytes);
                    observe(bytes); h2test::feed(*engine, bytes, clock_type::now()); progress = true;
                    executor.run_pending();
                } else if (read.state == hd::tls_session::progress::eof || read.state == hd::tls_session::progress::failed) {
                    return false;
                }
                engine->begin_turn(); executor.run_pending();
                auto out = engine->output(clock_type::now());
                if (!out.empty()) {
                    auto result = tls.write({reinterpret_cast<const std::byte*>(out.data()), out.size()});
                    if (result.bytes) {
                        engine->advance_output(result.bytes); progress = true;
                    }
                    if (result.state == hd::tls_session::progress::failed) return false;
                }
                if (engine->failure() && engine->output().empty()) terminal = true;
            }
            if (connected && terminal) {
                // Retire GOAWAY and TLS close_notify before dropping TCP. A
                // peer may already have sent the rejected frame's remaining
                // payload; consuming it avoids an abortive unread-data close.
                auto read = tls.read(buffer);
                if (read.state == hd::tls_session::progress::eof) return false;
                if (read.bytes) progress = true;
                tls.shutdown();
            }
            if (sent == ciphertext.size()) {
                ciphertext.clear(); sent = 0;
                auto n = tls.drain(buffer);
                if (n) {
                    ciphertext.assign(buffer.begin(), buffer.begin() + n); progress = true;
                }
            }
            if (sent < ciphertext.size()) {
                auto n = send(fd, ciphertext.data() + sent, ciphertext.size() - sent, 0);
                if (n > 0) {
                    sent += n; total += n; progress = true;
                } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    return false;
                }
            }
            if (!progress) break;
        }
        return true;
    }
};
}  // namespace
int main() {
    try {
        signal(SIGPIPE, SIG_IGN);
        hd::tls_credentials_registry registry;
        if (!registry.replace(tls_test::credentials()).ok()) throw std::runtime_error("TLS credentials");
        int listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 64)) throw std::runtime_error("loopback listen");
        socklen_t length = sizeof(address); getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
        fcntl(listener, F_SETFL, O_NONBLOCK); fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
        std::cout << "READY " << ntohs(address.sin_port) << std::endl;
        counters counts; h::resume_signal hold, upload;
        std::vector<std::unique_ptr<connection>> peers;
        std::string commands; unsigned accepted = 0;
        const auto deadline = clock_type::now() + std::chrono::minutes(15);
        bool running = true;
        while (running && clock_type::now() < deadline && accepted < 1024) {
            std::vector<pollfd> polls{{listener, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
            for (auto& peer : peers) polls.push_back({peer->fd, static_cast<std::int16_t>(POLLIN | (peer->sent < peer->ciphertext.size() ? POLLOUT : 0)), 0});
            if (poll(polls.data(), polls.size(), 20) < 0 && errno != EINTR) throw std::runtime_error("poll");
            if (polls[1].revents & POLLIN) {
                char input[1024]; auto n = read(STDIN_FILENO, input, sizeof(input));
                if (n <= 0) running = false;
                else commands.append(input, n);
                while (commands.find('\n') != std::string::npos) {
                    auto end = commands.find('\n'); auto command = commands.substr(0, end); commands.erase(0, end + 1);
                    if (command == "STOP") running = false;
                    else if (command == "RELEASE") hold.signal();
                    else if (command == "UPLOAD") upload.signal();
                    else if (command == "STATS")
                        std::cout << "STATS " << counts.started << ' ' << counts.completed << ' ' << counts.cancelled
                                  << ' ' << counts.uploaded << ' ' << counts.data << std::endl;
                    else throw std::runtime_error("unknown fixture command");
                }
                if (commands.size() > 4096) throw std::runtime_error("command limit");
            }
            if (polls[1].revents & POLLHUP) running = false;
            for (std::size_t i = peers.size(); i > 0; --i) {
                if (!peers[i - 1]->step(polls[i + 1].revents)) peers.erase(peers.begin() + i - 1);
            }
            if ((polls[0].revents & POLLIN) && peers.size() < 16) {
                int fd = accept(listener, nullptr, nullptr);
                if (fd >= 0) {
                    fcntl(fd, F_SETFL, O_NONBLOCK); ++accepted;
                    peers.push_back(std::make_unique<connection>(fd, registry.acquire()->select_default(), counts, hold, upload));
                }
            }
        }
        peers.clear(); close(listener);
        if (running) throw std::runtime_error("fixture bound exhausted");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
