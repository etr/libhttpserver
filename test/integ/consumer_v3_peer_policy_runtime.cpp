/*
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

#include <httpserver/server/server.hpp>
#include "./host_poll.hpp"

namespace {
namespace srv = httpserver::server;
namespace http = httpserver::http;

#if defined(_WIN32)
using socket_type = SOCKET;
constexpr auto invalid_socket = INVALID_SOCKET;
constexpr int send_flags = 0;
void close_socket(socket_type fd) { ::closesocket(fd); }
bool disconnected() {
    return ::WSAGetLastError() == WSAECONNRESET || ::WSAGetLastError() == WSAESHUTDOWN;
}
#else
using socket_type = int;
constexpr int invalid_socket = -1;
#if defined(MSG_NOSIGNAL)
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif
void close_socket(socket_type fd) { ::close(fd); }
bool disconnected() { return errno == EPIPE || errno == ECONNRESET; }
#endif

void require(bool condition, std::source_location where = std::source_location::current()) {
    if (!condition) throw std::runtime_error("public peer-policy contract failed at line " + std::to_string(where.line()));
}

class client {
 public:
    explicit client(std::uint16_t port, bool admin = false) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        require(fd_ != invalid_socket);
#if defined(_WIN32)
        DWORD bound = 5000;
        const auto* timeout = reinterpret_cast<const char*>(&bound);
#else
        timeval bound{5, 0};
        const auto* timeout = &bound;
#endif
#if defined(SO_NOSIGPIPE)
        int no_sigpipe = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, timeout, sizeof(bound));
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, timeout, sizeof(bound));
        sockaddr_in local{};
        local.sin_family = AF_INET;
        ::inet_pton(AF_INET, admin ? "127.0.0.2" : "127.0.0.1", &local.sin_addr);
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &remote.sin_addr);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0
            || ::connect(fd_, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) != 0) {
            close_socket(fd_);
            fd_ = invalid_socket;
            require(false);
        }
    }
    ~client() { if (fd_ != invalid_socket) close_socket(fd_); }
    client(const client&) = delete;
    client& operator=(const client&) = delete;

    void request(bool accepted) {
        const std::string request = "GET /policy HTTP/1.1\r\nHost: localhost\r\n\r\n";
        std::size_t sent = 0;
        while (sent < request.size()) {
            const auto n = ::send(fd_, request.data() + sent, request.size() - sent, send_flags);
            if (n <= 0) {
                require(!accepted && disconnected());
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        std::string wire;
        for (;;) {
            char buffer[1024];
            const auto n = ::recv(fd_, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                require(!accepted && wire.empty() && (n == 0 || disconnected()));
                return;
            }
            wire.append(buffer, static_cast<std::size_t>(n));
            require(accepted);
            const auto end = wire.find("\r\n\r\n");
            if (end != std::string::npos && wire.size() >= end + 4 + 9) {
                require(wire.starts_with("HTTP/1.1 200 "));
                require(wire.substr(end + 4) == "policy-ok");
                return;
            }
            require(wire.size() < 8192);
        }
    }

 private:
    socket_type fd_ = invalid_socket;
};

enum class change { none, deny, disable, enable, remove_deny, reject_all, allow, accept_all, remove_allow };

void scenario(srv::loop_mode mode) {
    std::atomic<change> next{change::none};
    std::atomic<int> accepted_hooks{0}, refused_hooks{0}, cancelled{0};
    std::atomic<bool> valid{true};
    srv::server_options options;
    options.loop() = mode;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    auto accept_hook = server.hooks().add<srv::hook_phase::accept_decision>([&](srv::accept_decision_ctx& ctx) {
        (ctx.accepted ? accepted_hooks : refused_hooks).fetch_add(1);
        if ((ctx.accepted && ctx.reason != srv::peer_refusal::none)
            || (!ctx.accepted && ctx.reason == srv::peer_refusal::none)) valid = false;
        return srv::hook_action::pass();
    });
    auto completed_hook = server.hooks().add<srv::hook_phase::request_completed>([&](srv::request_completed_ctx& ctx) {
        if (ctx.end.code() == http::outcome_code::peer_refused) cancelled.fetch_add(1);
        return srv::hook_action::pass();
    });
    require(server.route_sync(http::method::known(http::method_id::get), "/policy",
        [&](const http::request_head&, std::span<const std::byte>) {
            auto& policy = server.peer_policy();
            switch (next.exchange(change::none)) {
                case change::deny: require(policy.deny("127.0.0.1").ok()); break;
                case change::disable: policy.set_enabled(false); break;
                case change::enable: policy.set_enabled(true); break;
                case change::remove_deny: require(policy.remove_denied("127.0.0.1").ok()); break;
                case change::reject_all:
                    require(policy.allow("127.0.0.2").ok());
                    policy.set_mode(srv::peer_policy_mode::reject_all); break;
                case change::allow: require(policy.allow("127.0.0.1").ok()); break;
                case change::accept_all: policy.set_mode(srv::peer_policy_mode::accept_all); break;
                case change::remove_allow: require(policy.remove_allowed("127.0.0.1").ok()); break;
                case change::none: break;
            }
            srv::sync_response response;
            response.status = http::status::from_code(200);
            const std::string body = "policy-ok";
            response.fields.append("Content-Length", std::to_string(body.size()));
            const auto bytes = std::as_bytes(std::span(body));
            response.body.assign(bytes.begin(), bytes.end());
            return response;
        }, 1024).ok());
    require(server.listen().ok());
    std::thread host;
    if (mode == srv::loop_mode::external) {
        host = std::thread([&] {
            try {
                auto* driver = server.readiness();
                auto snapshot = driver->interests();
                while (server.is_running()) {
                    auto events = v3_host::wait(snapshot);
                    if (!driver->dispatch(events, v3_host::clock_type::now()).ok()) {
                        valid = false;
                        break;
                    }
                    snapshot = driver->interests();
                }
            } catch (...) { valid = false; }
        });
    }
    auto cleanup = [&] {
        server.request_stop();
        if (host.joinable()) host.join();
        server.stop();
    };
    try {
        const auto port = server.get_bound_port(0);
        client admin(port, true);
        client persistent(port);
        admin.request(true);
        persistent.request(true);
        auto update = [&](change operation) {
            next = operation;
            admin.request(true);  // response follows completed handler mutation
        };
        auto probe = [&](bool accepted) { client connection(port); connection.request(accepted); };
        update(change::deny);
        persistent.request(false);  // revalidation of an already admitted connection
        probe(false);
        update(change::disable); probe(true);
        update(change::enable); probe(false);
        update(change::remove_deny); probe(true);
        update(change::reject_all); probe(false);
        update(change::allow); probe(true);
        update(change::deny); probe(false);  // reject_all: deny wins
        update(change::accept_all); probe(true);  // accept_all: allow wins
        update(change::remove_allow); probe(false);
        update(change::remove_deny); probe(true);
        cleanup();
        std::printf("public peer policy: loop=%s valid=%d accepted_hooks=%d refused_hooks=%d peer_refused=%d\n",
            mode == srv::loop_mode::external ? "external" : "managed", valid.load(),
            accepted_hooks.load(), refused_hooks.load(), cancelled.load());
        require(valid.load() && accepted_hooks.load() >= 7 && refused_hooks.load() >= 5);
        require(cancelled.load() >= 1);
    } catch (...) { cleanup(); throw; }
}
}  // namespace

int main() {
#if defined(_WIN32)
    WSADATA data;
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
    try {
        scenario(srv::loop_mode::managed);
        scenario(srv::loop_mode::external);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
