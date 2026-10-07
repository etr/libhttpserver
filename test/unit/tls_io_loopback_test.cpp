/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <httpserver/detail/io_kqueue_backend.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include "./io_loopback.hpp"
#include "./tls_io_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
namespace srv = httpserver::server;
namespace ps = hd::pollsys;
using std::chrono_literals::operator""s;
namespace {
// Independent peer uses socket BIOs, rather than the adapter or its BIO pair.
struct peer {
    ps::native_socket_t socket;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{SSL_CTX_new(TLS_client_method()), SSL_CTX_free};
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl{SSL_new(context.get()), SSL_free};
    explicit peer(ps::native_socket_t fd) : socket(fd) {
        ps::set_nonblocking(socket, true);
        SSL_set_fd(ssl.get(), static_cast<int>(socket));
    }
    ~peer() {
        ps::close_socket(socket);
    }
    bool classify(int rc) {
        if (rc == 1) {
            return true;
        }
        const int error = SSL_get_error(ssl.get(), rc);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            throw std::runtime_error("independent TLS peer failed");
        }
        return false;
    }
};
void external_tick(hd::io_socket_backend& raw) {
    const auto snapshot = raw.interests();
    auto interests = snapshot.sockets;
    if (snapshot.wake) {
        interests.push_back(*snapshot.wake);
    }
    std::vector<ps::poll_slot> slots;
    for (const auto& interest : interests) {
        const auto events = static_cast<ps::event_mask>((interest.readable ? ps::k_readable : 0) | (interest.writable ? ps::k_writable : 0));
        slots.push_back({static_cast<ps::native_socket_t>(interest.handle.value), events, 0});
    }
    ps::poll_call(slots.data(), slots.size(), 1);
    std::vector<srv::readiness_event> events;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto flags = slots[i].revents;
        if (!flags) {
            continue;
        }
        events.push_back({interests[i].key, interests[i].generation, (flags & ps::k_readable) != 0, (flags & ps::k_writable) != 0, (flags & ps::k_poll_hangup) != 0,
                          (flags & ps::k_poll_error) != 0});
    }
    raw.dispatch(events, std::chrono::steady_clock::now());
}
void scenario(littletest::test_runner* __lt_tr__, const char* __lt_name__, bool kqueue, bool external) {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::unique_ptr<hd::io_socket_backend> raw;
#if defined(__APPLE__) || defined(__FreeBSD__)
    if (kqueue) {
        raw = std::make_unique<hd::io_kqueue_backend>();
    }
#endif
    if (!raw) {
        raw = std::make_unique<hd::io_poll_backend>(external ? srv::loop_mode::external : srv::loop_mode::managed);
    }
    if (external) {
        raw->activate_external();
    }
    auto listener = io_loopback::listener::open();
    LT_ASSERT(listener.ok());
    LT_CHECK(listener.port() != 0);
    peer client(io_loopback::connect_to(listener.port()));
    LT_ASSERT(client.socket != ps::k_invalid_socket);
    ps::native_socket_t accepted = ps::k_invalid_socket;
    LT_ASSERT(ps::accept_one(listener.socket(), &accepted).status == ps::sys_status::ok);
    raw->adopt_connection(1, accepted);
    hd::tls_io_backend tls(*raw, ex, 1, hd::tls_context::server_pem(tls_test::pem("cert.pem"), tls_test::pem("key.pem")), true);
    auto tick = [&] {
        ex.run_pending();
        if (external) {
            external_tick(*raw);
        } else {
            std::this_thread::yield();
        }
    };
    auto wait = [&](auto step, auto done) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!done() && std::chrono::steady_clock::now() < deadline) {
            step();
            tick();
        }
        ex.run_pending();
        return done();
    };
    hd::tls_handshake_operation handshake(owner, 1, std::chrono::steady_clock::now() + 5s);
    handshake.submit(tls);
    bool connected = false;
    LT_ASSERT(wait(
        [&] {
            if (!connected) {
                ERR_clear_error();
                connected = client.classify(SSL_connect(client.ssl.get()));
            }
        },
        [&] { return connected && handshake.state()->applied(); }));
    LT_CHECK(handshake.state()->stored_result().code == hh::outcome_code::ok);
    const std::string message = "independent OpenSSL loopback peer";
    std::array<std::byte, 100> received{};
    hd::read_operation read(owner, 1, received, std::chrono::steady_clock::now() + 5s);
    read.submit(tls);
    bool sent = false;
    LT_ASSERT(wait(
        [&] {
            if (!sent) {
                std::size_t count = 0;
                ERR_clear_error();
                sent = client.classify(SSL_write_ex(client.ssl.get(), message.data(), message.size(), &count));
            }
        },
        [&] { return sent && read.state()->applied(); }));
    LT_CHECK(read.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK_EQ(read.state()->stored_result().transferred, message.size());
    LT_CHECK(std::equal(message.begin(), message.end(), reinterpret_cast<const char*>(received.data())));
    hd::write_operation write(owner, 1, std::as_bytes(std::span(message)), std::chrono::steady_clock::now() + 5s);
    write.submit(tls);
    std::array<char, 100> echoed{};
    std::size_t count = 0;
    bool got = false;
    LT_ASSERT(wait(
        [&] {
            if (!got) {
                ERR_clear_error();
                got = client.classify(SSL_read_ex(client.ssl.get(), echoed.data(), echoed.size(), &count));
            }
        },
        [&] { return got && write.state()->applied(); }));
    LT_CHECK_EQ(count, message.size());
    LT_CHECK(std::equal(message.begin(), message.end(), echoed.begin()));
    LT_CHECK(write.state()->stored_result().code == hh::outcome_code::ok);
    hd::tls_shutdown_operation shutdown(owner, 1, std::chrono::steady_clock::now() + 5s);
    shutdown.submit(tls);
    bool closed = false;
    LT_ASSERT(wait(
        [&] {
            if (!closed) {
                ERR_clear_error();
                const int rc = SSL_shutdown(client.ssl.get());
                closed = rc == 0 ? false : client.classify(rc);
            }
        },
        [&] { return closed && shutdown.state()->applied(); }));
    LT_CHECK(shutdown.state()->stored_result().code == hh::outcome_code::ok);
    tls.close();
    raw->release_connection(1);
    raw->close();
    ex.run_pending();
}
}  // namespace
LT_BEGIN_SUITE(tls_io_loopback_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_io_loopback_suite)
LT_BEGIN_AUTO_TEST(tls_io_loopback_suite, poll_peer_roundtrip)
    scenario(__lt_tr__, __lt_name__, false, false);
LT_END_AUTO_TEST(poll_peer_roundtrip)
LT_BEGIN_AUTO_TEST(tls_io_loopback_suite, external_readiness_peer_roundtrip)
    scenario(__lt_tr__, __lt_name__, false, true);
LT_END_AUTO_TEST(external_readiness_peer_roundtrip)
#if defined(__APPLE__) || defined(__FreeBSD__)
LT_BEGIN_AUTO_TEST(tls_io_loopback_suite, kqueue_peer_roundtrip)
    scenario(__lt_tr__, __lt_name__, true, false);
LT_END_AUTO_TEST(kqueue_peer_roundtrip)
#endif
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
