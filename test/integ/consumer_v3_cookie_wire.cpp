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

#include <httpserver/http/cookie.hpp>
#include <httpserver/http.hpp>
#include <httpserver/server/server.hpp>
#include "./littletest.hpp"
#include "./v3_host_poll.hpp"

namespace {
namespace srv = httpserver::server;
namespace http = httpserver::http;

#if defined(_WIN32)
using socket_type = SOCKET;
constexpr auto invalid_socket = INVALID_SOCKET;
constexpr int send_flags = 0;
void close_socket(socket_type fd) { ::closesocket(fd); }
#else
using socket_type = int;
constexpr int invalid_socket = -1;
#if defined(MSG_NOSIGNAL)
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif
void close_socket(socket_type fd) { ::close(fd); }
#endif

void require(bool condition, std::source_location where = std::source_location::current()) {
    if (!condition) throw std::runtime_error("public cookie wire contract failed at line " + std::to_string(where.line()));
}

class client {
 public:
    explicit client(std::uint16_t port) {
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
        ::inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
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

    std::string exchange(std::string_view request) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::size_t sent = 0;
        while (sent < request.size()) {
            const auto n = ::send(fd_, request.data() + sent, request.size() - sent, send_flags);
            require(n > 0);
            sent += static_cast<std::size_t>(n);
        }
        std::string wire;
        for (;;) {
            char buffer[1024];
            const auto n = ::recv(fd_, buffer, sizeof(buffer), 0);
            require(n >= 0);
            if (n == 0) return wire;
            wire.append(buffer, static_cast<std::size_t>(n));
            require(wire.size() < 8192 && std::chrono::steady_clock::now() < deadline);
        }
    }

 private:
    socket_type fd_ = invalid_socket;
};

bool scenario(srv::loop_mode mode) {
    std::atomic<bool> valid{true};
    srv::server_options options;
    options.loop() = mode;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 0, false});
    srv::native_server server(options);
    require(server.route_sync(http::method::known(http::method_id::get), "/cookies",
        [&](const http::request_head& head, std::span<const std::byte>) {
            srv::sync_response response;
            response.status = http::status::from_code(200);
            for (const auto& field : head.head_fields.all("cookie")) {
                for (auto value : http::parse_cookie_header(field)) {
                    value.expires = 784111777;
                    value.path = "/";
                    value.same_site = http::same_site_mode::none;
                    std::string rendered;
                    require(http::render_set_cookie(value, rendered).ok());
                    response.fields.append("Set-Cookie", rendered);
                }
            }
            response.fields.append("X-Cookie-End", "yes");
            return response;
        }, 1024).ok());
    require(server.route_sync(http::method::known(http::method_id::get), "/reject",
        [&](const http::request_head&, std::span<const std::byte>) {
            const auto raw = http::parse_cookie_header("sid=ab\r\nSet-Cookie: injected=1\r\n\r\nHTTP/1.1 200 OK");
            require(raw.size() == 1 && raw[0].value.find('\r') != std::string::npos);
            std::string output;
            const auto result = http::render_set_cookie(raw[0], output);
            require(result.code() == http::outcome_code::invalid_argument && output.empty());
            srv::sync_response response;
            response.status = http::status::from_code(422);
            if (result.ok()) response.fields.append("Set-Cookie", output);
            response.fields.append("X-Cookie-Rejected", "yes");
            return response;
        }, 1024).ok());
    require(server.listen().ok());
    std::thread host;
    if (mode == srv::loop_mode::external) {
        host = std::thread([&] {
            try {
                const auto deadline = v3_host::clock_type::now() + std::chrono::seconds(10);
                while (server.is_running()) {
                    const auto snapshot = server.readiness()->interests();
                    auto events = v3_host::wait(snapshot, deadline);
                    if (v3_host::clock_type::now() >= deadline
                        || !server.readiness()->dispatch(events, v3_host::clock_type::now()).ok()) {
                        valid = false;
                        break;
                    }
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
        client roundtrip(port);
        const auto wire = roundtrip.exchange("GET /cookies HTTP/1.1\r\nHost: local\r\n"
            "Cookie: SID=a=b%20c; empty=\r\nCookie: SID=two\r\nConnection: close\r\n\r\n");
        require(wire.starts_with("HTTP/1.1 200 "));
        const std::string suffix = "; Expires=Sun, 06 Nov 1994 08:49:37 GMT; Path=/; Secure; SameSite=None\r\n";
        const std::string expected = "Set-Cookie: SID=a=b%20c" + suffix
            + "Set-Cookie: empty=" + suffix + "Set-Cookie: SID=two" + suffix + "X-Cookie-End: yes\r\n";
        const auto begin = wire.find(expected);
        require(begin != std::string::npos);
        require(wire.find("Set-Cookie:") == begin);
        require(wire.find("Set-Cookie:", begin + expected.size()) == std::string::npos);
        require(wire.find("HTTP/1.1", 1) == std::string::npos);
        require(wire.ends_with("\r\n\r\n"));
        client reject(port);
        const auto rejected = reject.exchange("GET /reject HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n");
        require(rejected.starts_with("HTTP/1.1 422 "));
        require(rejected.find("X-Cookie-Rejected: yes\r\n") != std::string::npos);
        require(rejected.find("Set-Cookie:") == std::string::npos);
        require(rejected.find("HTTP/1.1", 1) == std::string::npos);
        require(rejected.ends_with("\r\n\r\n"));
        cleanup();
        require(valid.load());
        if (mode == srv::loop_mode::external) {
            require(server.readiness()->interests().sockets.empty());
            require(!server.readiness()->interests().wake);
        }
    } catch (...) { cleanup(); throw; }
    return !server.is_running();
}
}  // namespace

LT_BEGIN_SUITE(cookie_wire_suite)
    void set_up() {
#if defined(_WIN32)
        WSADATA data;
        require(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif
    }
    void tear_down() {
#if defined(_WIN32)
        ::WSACleanup();
#endif
    }
LT_END_SUITE(cookie_wire_suite)

LT_BEGIN_AUTO_TEST(cookie_wire_suite, managed_cookie_fields_and_injection_rejection)
    LT_CHECK(scenario(srv::loop_mode::managed));
LT_END_AUTO_TEST(managed_cookie_fields_and_injection_rejection)

LT_BEGIN_AUTO_TEST(cookie_wire_suite, external_cookie_fields_and_injection_rejection)
    LT_CHECK(scenario(srv::loop_mode::external));
LT_END_AUTO_TEST(external_cookie_fields_and_injection_rejection)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
