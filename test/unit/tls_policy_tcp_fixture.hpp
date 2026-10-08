/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_TLS_POLICY_TCP_FIXTURE_HPP_
#define TEST_UNIT_TLS_POLICY_TCP_FIXTURE_HPP_
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <source_location>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <httpserver/detail/io_kqueue_backend.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include "./io_loopback.hpp"
#include "./tls_acme_peer.hpp"
#include "./tls_psk_fixture.hpp"
namespace tcp_policy {
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
namespace ps = hd::pollsys;
using clock = std::chrono::steady_clock;
using std::chrono_literals::operator""s;
inline void require(bool value, std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error("TCP TLS policy invariant failed at line " + std::to_string(location.line()));
}
// No littletest assertions on worker threads; unwind connections before reporting.
struct gate {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned phase = 0;
    bool aborted = false;
    bool wait(unsigned expected) {
        std::unique_lock lock(mutex);
        return changed.wait_until(lock, clock::now() + 5s, [&] { return phase >= expected || aborted; }) && !aborted;
    }
    void signal(unsigned next) { std::lock_guard lock(mutex); phase = next; changed.notify_all(); }
    void abort() { std::lock_guard lock(mutex); aborted = true; changed.notify_all(); }
};
inline void dispatch(hd::io_socket_backend& raw) {
    const auto snapshot = raw.interests();
    auto interests = snapshot.sockets;
    if (snapshot.wake) interests.push_back(*snapshot.wake);
    std::vector<ps::poll_slot> slots;
    for (const auto& i : interests) {
        slots.push_back({static_cast<ps::native_socket_t>(i.handle.value),
            static_cast<ps::event_mask>((i.readable ? ps::k_readable : 0) | (i.writable ? ps::k_writable : 0)), 0});
    }
    ps::poll_call(slots.data(), slots.size(), 1);
    std::vector<httpserver::server::readiness_event> events;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto flags = slots[i].revents;
        if (flags) events.push_back({interests[i].key, interests[i].generation, (flags & ps::k_readable) != 0, (flags & ps::k_writable) != 0,
                                    (flags & ps::k_poll_hangup) != 0, (flags & ps::k_poll_error) != 0});
    }
    raw.dispatch(events, clock::now());
}
class observed_transport final : public hd::io_backend {
    hd::io_socket_backend& raw_;
 public:
    std::atomic<bool> read_submitted{false};
    explicit observed_transport(hd::io_socket_backend& raw) : raw_(raw) {}
    void submit(hd::op_state& operation) override {
        if (operation.kind() == hd::io_op_kind::read) read_submitted = true;
        raw_.submit(operation);
    }
    hh::outcome_code request_cancel(hd::op_state& operation) override { return raw_.request_cancel(operation); }
};
struct gate_release {
    gate& value;
    ~gate_release() { value.abort(); }
};
struct socket_owner {
    ps::native_socket_t value = ps::k_invalid_socket;
    ~socket_owner() { if (value != ps::k_invalid_socket) ps::close_socket(value); }
};
struct connection {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner{ex};
    std::unique_ptr<hd::io_socket_backend> raw;
    std::unique_ptr<observed_transport> transport;
    io_loopback::listener listener = io_loopback::listener::open();
    socket_owner socket;
    mtls_test::client_context context;
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    std::unique_ptr<hd::tls_io_backend> tls;
    hd::tls_handshake_operation handshake{owner, 1, clock::now() + 5s};
    std::uint64_t generation;
    bool external;
    bool peer_failed = false;
    bool submitted = false;
    const clock::time_point scenario_deadline = clock::now() + 20s;
    connection(hd::tls_credentials_selection selected, mtls_test::client_context ctx, const char* name = "a.example",
               const std::vector<std::string>& offers = {"h2", "http/1.1"}, bool external_mode = true, bool kqueue = false,
               hd::tls_handshake_context transport = {hd::tls_transport::tcp, 443})
        : context(std::move(ctx)), generation(selected.snapshot->generation()), external(external_mode) {
        require(listener.ok());
#if defined(__APPLE__) || defined(__FreeBSD__)
        if (kqueue) raw = std::make_unique<hd::io_kqueue_backend>();
#else
        (void)kqueue;
#endif
        if (!raw) raw = std::make_unique<hd::io_poll_backend>(external ? httpserver::server::loop_mode::external : httpserver::server::loop_mode::managed);
        if (external) raw->activate_external();
        socket.value = io_loopback::connect_to(listener.port());
        require(socket.value != ps::k_invalid_socket);
        socket_owner accepted;
        const auto accept_deadline = clock::now() + 5s;
        while (accepted.value == ps::k_invalid_socket && clock::now() < accept_deadline) {
            ps::accept_one(listener.socket(), &accepted.value);
            if (accepted.value == ps::k_invalid_socket) {
                ps::poll_slot slot{listener.socket(), ps::k_readable, 0};
                ps::poll_call(&slot, 1, 1);
            }
        }
        require(accepted.value != ps::k_invalid_socket);
        raw->adopt_connection(1, std::exchange(accepted.value, ps::k_invalid_socket));
        ps::set_nonblocking(socket.value, true);
        peer.reset(SSL_new(context.get()));
        require(peer && SSL_set_fd(peer.get(), static_cast<int>(socket.value)) == 1);
        if (name) SSL_set_tlsext_host_name(peer.get(), name);
        std::vector<unsigned char> wire;
        for (const auto& offer : offers) {
            wire.push_back(static_cast<unsigned char>(offer.size()));
            wire.insert(wire.end(), offer.begin(), offer.end());
        }
        if (!wire.empty()) SSL_set_alpn_protos(peer.get(), wire.data(), static_cast<unsigned>(wire.size()));
        // Zero requests the actual ephemeral TCP listener metadata for negative ACME cases.
        if (transport.transport == hd::tls_transport::tcp && transport.local_port == 0) transport.local_port = listener.port();
        this->transport = std::make_unique<observed_transport>(*raw);
        tls = std::make_unique<hd::tls_io_backend>(*this->transport, ex, 1, std::move(selected), true, transport);
    }
    ~connection() {
        if (tls) tls->close();
        if (raw) {
            raw->release_connection(1);
            raw->close();
        }
        ex.run_pending();
        tls.reset();
        ex.run_pending();
        peer.reset();
    }
    void tick() {
        ex.run_pending();
        if (external) dispatch(*raw);
        else std::this_thread::yield();
    }
    template<class Step, class Done> bool until(Step step, Done done) {
        const auto deadline = std::min(clock::now() + 5s, scenario_deadline);
        while (!done() && clock::now() < deadline) {
            step();
            tick();
        }
        ex.run_pending();
        return done();
    }
    void start() {
        if (!submitted) {
            handshake.submit(*tls);
            submitted = true;
        }
    }
    bool park() {
        start();
        return until([] {}, [&] {
            return !handshake.state()->applied() && transport->read_submitted.load();
        });
    }
    bool classify(int rc) {
        if (rc == 1) return true;
        const int error = SSL_get_error(peer.get(), rc);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) peer_failed = true;
        return false;
    }
    bool connect() {
        start();
        const bool terminal = until([&] {
            if (!peer_failed && !SSL_is_init_finished(peer.get())) {
                ERR_clear_error();
                classify(SSL_connect(peer.get()));
            }
        }, [&] {
            return handshake.state()->applied() && (handshake.state()->stored_result().code != hh::outcome_code::ok || peer_failed || SSL_is_init_finished(peer.get()));
        });
        return terminal && !peer_failed && SSL_is_init_finished(peer.get()) && handshake.state()->stored_result().code == hh::outcome_code::ok;
    }
    hh::outcome_code result() const { return handshake.state()->stored_result().code; }
    std::int64_t serial() const {
        auto* cert = SSL_get0_peer_certificate(peer.get());
        return cert ? ASN1_INTEGER_get(X509_get_serialNumber(cert)) : -1;
    }
    std::string alpn() const {
        const unsigned char* data = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(peer.get(), &data, &size);
        return size ? std::string(reinterpret_cast<const char*>(data), size) : std::string{};
    }
    bool digest_matches(const hd::tls_acme_challenge& challenge) const {
        auto* cert = SSL_get0_peer_certificate(peer.get());
        if (!cert) return false;
        const int index = acme_test::acme_index(cert);
        if (index < 0) return false;
        auto* ext = X509_get_ext(cert, index);
        auto* bytes = X509_EXTENSION_get_data(ext);
        return X509_EXTENSION_get_critical(ext) && ASN1_STRING_length(bytes) == 34 &&
            std::memcmp(ASN1_STRING_get0_data(bytes) + 2, challenge.key_authorization_sha256.data(), 32) == 0;
    }
    bool exchange() {
        const std::string message = "ordinary authenticated bytes";
        std::array<std::byte, 64> received{};
        hd::read_operation read(owner, 1, received, clock::now() + 5s);
        read.submit(*tls);
        bool sent = false;
        const bool delivered = until([&] {
            if (!sent) {
                std::size_t count = 0;
                ERR_clear_error();
                sent = classify(SSL_write_ex(peer.get(), message.data(), message.size(), &count));
            }
        }, [&] { return read.state()->applied() && sent; });
        if (!delivered) return false;
        if (read.state()->stored_result().code != hh::outcome_code::ok || read.state()->stored_result().transferred != message.size() ||
            std::memcmp(received.data(), message.data(), message.size()) != 0) return false;
        hd::write_operation write(owner, 1, std::as_bytes(std::span(message)), clock::now() + 5s);
        write.submit(*tls);
        std::array<char, 64> echoed{};
        std::size_t count = 0;
        bool got = false;
        const bool echoed_back = until([&] {
            if (!got) {
                ERR_clear_error();
                got = classify(SSL_read_ex(peer.get(), echoed.data(), echoed.size(), &count));
            }
        }, [&] { return got && write.state()->applied(); });
        return echoed_back && write.state()->stored_result().code == hh::outcome_code::ok &&
            count == message.size() && std::memcmp(echoed.data(), message.data(), count) == 0;
    }
};
struct runtime_owner {
    std::shared_ptr<hd::tls_psk_runtime> runtime = std::make_shared<hd::tls_psk_runtime>();
    ~runtime_owner() { runtime->stop(); runtime->drain(clock::now() + 5s); }
};
}  // namespace tcp_policy
#endif  // TEST_UNIT_TLS_POLICY_TCP_FIXTURE_HPP_
