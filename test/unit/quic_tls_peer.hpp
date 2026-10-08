/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_QUIC_TLS_PEER_HPP_
#define TEST_UNIT_QUIC_TLS_PEER_HPP_
#include <openssl/core_dispatch.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/quic_tls_session.hpp>
#include "./tls_mtls_fixture.hpp"
namespace quic_test {
using session_ptr = std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)>;
using context_ptr = std::shared_ptr<SSL_CTX>;
inline constexpr std::array client_parameters{std::byte{15}, std::byte{1}, std::byte{7}};
inline constexpr std::array server_parameters{std::byte{0}, std::byte{1}, std::byte{9}, std::byte{15}, std::byte{1}, std::byte{8}};
inline hd::quic_tls_config configuration() {
    hd::quic_tls_config result;
    result.output_capacity = 127;
    result.receive_lease_capacity = 53;
    result.local_parameters = server_parameters;
    result.peer_cids.initial_source = std::span(client_parameters).last(1);
    return result;
}
inline hd::tls_credentials_config credentials() {
    auto config = tls_test::credentials();
    config.hosts[0].alpn = {"h2", "http/1.1", "h3"};
    return config;
}
inline context_ptr client_context() { return mtls_test::client(TLS1_3_VERSION); }
// Independent adapter: no production callbacks/reassembly/buffering are reused.
struct peer {
    std::array<std::vector<std::byte>, 3> input, output;
    std::array<std::array<std::vector<std::byte>, 2>, 3> secrets;
    std::vector<std::byte> lease, parameters;
    unsigned read_level = 0, write_level = 0, early_secrets = 0;
    bool offered_early_data = false, failed = false;
    context_ptr ctx;
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl{nullptr, SSL_free};
    template<class Function>
    static int boundary(void* arg, Function callback) noexcept {
        auto& self = *static_cast<peer*>(arg);
        try {
            return callback(self);
        } catch (...) {
            self.failed = true;
            return 0;
        }
    }
    static int send(SSL*, const unsigned char* data, std::size_t size, std::size_t* consumed, void* arg) noexcept {
        *consumed = 0;
        return boundary(arg, [&](auto& self) {
            auto& out = self.output[self.write_level];
            const auto* first = reinterpret_cast<const std::byte*>(data);
            out.insert(out.end(), first, first + size);
            *consumed = size;
            return 1;
        });
    }
    static int receive(SSL*, const unsigned char** data, std::size_t* size, void* arg) noexcept {
        *data = nullptr;
        *size = 0;
        return boundary(arg, [&](auto& self) {
            if (!self.lease.empty()) return 0;
            self.lease.swap(self.input[self.read_level]);
            *data = reinterpret_cast<const unsigned char*>(self.lease.data());
            *size = self.lease.size();
            return 1;
        });
    }
    static int release(SSL*, std::size_t size, void* arg) noexcept {
        return boundary(arg, [&](auto& self) {
            if (size != self.lease.size()) return 0;
            self.lease.clear();
            return 1;
        });
    }
    static int secret(SSL*, std::uint32_t level, int direction, const unsigned char* data, std::size_t size, void* arg) noexcept {
        return boundary(arg, [&](auto& self) {
            if (level == OSSL_RECORD_PROTECTION_LEVEL_EARLY) {
                ++self.early_secrets;
                return 1;
            }
            if (level != OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE && level != OSSL_RECORD_PROTECTION_LEVEL_APPLICATION) return 0;
            const auto index = level == OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE ? 1U : 2U;
            const auto* first = reinterpret_cast<const std::byte*>(data);
            self.secrets[index][direction].assign(first, first + size);
            (direction ? self.write_level : self.read_level) = index;
            return 1;
        });
    }
    static int transport_parameters(SSL*, const unsigned char* data, std::size_t size, void* arg) noexcept {
        return boundary(arg, [&](auto& self) {
            const auto* first = reinterpret_cast<const std::byte*>(data);
            self.parameters.assign(first, first + size);
            return 1;
        });
    }
    static int alert(SSL*, unsigned char, void* arg) noexcept { static_cast<peer*>(arg)->failed = true; return 1; }
    static void message(int writing, int, int type, const void* data, std::size_t size, SSL*, void* arg) noexcept {
        if (!writing || type != SSL3_RT_HANDSHAKE || size < 4) return;
        const auto* bytes = static_cast<const unsigned char*>(data);
        if (bytes[0] != SSL3_MT_CLIENT_HELLO) return;
        // Parse the actual outgoing ClientHello extension list, without editing it.
        std::size_t pos = 38;
        if (pos >= size) return;
        pos += 1 + bytes[pos];
        if (pos + 2 > size) return;
        auto u16 = [&](std::size_t at) { return (unsigned{bytes[at]} << 8) | bytes[at + 1]; };
        pos += 2 + u16(pos);
        if (pos >= size) return;
        pos += 1 + bytes[pos];
        if (pos + 2 > size) return;
        pos += 2;
        while (pos + 4 <= size) {
            const auto type_id = u16(pos), length = u16(pos + 2);
            pos += 4;
            if (length > size - pos) return;
            if (type_id == TLSEXT_TYPE_early_data) static_cast<peer*>(arg)->offered_early_data = true;
            pos += length;
        }
    }
    peer(context_ptr context, const char* hostname, const std::vector<std::string>& offers, SSL_SESSION* ticket, bool early)
        : ctx(std::move(context)), ssl(SSL_new(ctx.get()), SSL_free) {
        static const OSSL_DISPATCH callbacks[] = {
            {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_SEND, reinterpret_cast<void (*)(void)>(send)},
            {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RECV_RCD, reinterpret_cast<void (*)(void)>(receive)},
            {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RELEASE_RCD, reinterpret_cast<void (*)(void)>(release)},
            {OSSL_FUNC_SSL_QUIC_TLS_YIELD_SECRET, reinterpret_cast<void (*)(void)>(secret)},
            {OSSL_FUNC_SSL_QUIC_TLS_GOT_TRANSPORT_PARAMS, reinterpret_cast<void (*)(void)>(transport_parameters)},
            {OSSL_FUNC_SSL_QUIC_TLS_ALERT, reinterpret_cast<void (*)(void)>(alert)},
            {0, nullptr}
        };
        if (!ssl || SSL_set_quic_tls_cbs(ssl.get(), callbacks, this) != 1 ||
            SSL_set_quic_tls_transport_params(ssl.get(), reinterpret_cast<const unsigned char*>(client_parameters.data()), client_parameters.size()) != 1) {
            throw std::runtime_error("test QUIC TLS peer unavailable");
        }
        SSL_set_connect_state(ssl.get());
        if (hostname && (!SSL_set_tlsext_host_name(ssl.get(), hostname) || !SSL_set1_host(ssl.get(), hostname))) {
            throw std::runtime_error("test peer hostname unavailable");
        }
        std::vector<unsigned char> alpn;
        for (const auto& offer : offers) {
            alpn.push_back(static_cast<unsigned char>(offer.size()));
            alpn.insert(alpn.end(), offer.begin(), offer.end());
        }
        if (!alpn.empty() && SSL_set_alpn_protos(ssl.get(), alpn.data(), alpn.size()) != 0) throw std::runtime_error("test ALPN unavailable");
        if (ticket && SSL_set_session(ssl.get(), ticket) != 1) throw std::runtime_error("test ticket unavailable");
        if (SSL_set_quic_tls_early_data_enabled(ssl.get(), early) != 1) throw std::runtime_error("test early-data policy unavailable");
        SSL_set_msg_callback(ssl.get(), message);
        SSL_set_msg_callback_arg(ssl.get(), this);
    }
    bool step() {
        ERR_clear_error();
        int rc;
        if (SSL_is_init_finished(ssl.get())) {
            std::size_t count = 0;
            rc = SSL_read_ex(ssl.get(), nullptr, 0, &count);
        } else {
            rc = SSL_do_handshake(ssl.get());
        }
        if (rc != 1) {
            const auto error = SSL_get_error(ssl.get(), rc);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) failed = true;
        }
        return !failed;
    }
    std::string alpn() const {
        const unsigned char* value = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(ssl.get(), &value, &size);
        return size ? std::string(reinterpret_cast<const char*>(value), size) : std::string{};
    }
    std::int64_t serial() const { return ASN1_INTEGER_get(X509_get_serialNumber(SSL_get0_peer_certificate(ssl.get()))); }
    session_ptr ticket() const { return {SSL_get1_session(ssl.get()), SSL_SESSION_free}; }
};
struct connection {
    httpserver::server::resource_budget budget = httpserver::server::resource_budget::root({});
    hd::quic_key_state keys;
    hd::quic_tls_session server;
    peer client;
    std::array<std::uint64_t, 3> client_offsets{}, server_offsets{};
    bool done = false;
    connection(hd::tls_credentials_selection selection, context_ptr ctx = client_context(), const char* name = "a.example",
               const std::vector<std::string>& offers = {"http/1.1", "h2", "h3"}, SSL_SESSION* ticket = nullptr, bool early = false)
        : server(std::move(selection), configuration(), budget, keys), client(std::move(ctx), name, offers, ticket, early) {}
    bool deliver_client(std::size_t maximum = 4096) {
        for (unsigned level = 0; level < 3; ++level) {
            auto& pending = client.output[level];
            const auto count = std::min(pending.size(), maximum);
            // Reverse 17-byte fragments within the level; fill gaps last.
            for (std::size_t end = count; end > 0;) {
                const auto start = end > 17 ? end - 17 : 0;
                if (!server.receive(static_cast<hd::quic_crypto_level>(level), client_offsets[level] + start, std::span(pending).subspan(start, end - start))) return false;
                end = start;
            }
            client_offsets[level] += count;
            pending.erase(pending.begin(), pending.begin() + count);
        }
        return true;
    }
    bool deliver_server(bool& moved) {
        std::array<std::byte, 19> bytes{};
        for (unsigned level = 0; level < 3; ++level) {
            for (;;) {
                auto copy = server.copy_output(static_cast<hd::quic_crypto_level>(level), server_offsets[level], bytes);
                if (!copy) return false;
                if (!copy.bytes) break;
                moved = true;
                client.input[level].insert(client.input[level].end(), bytes.begin(), bytes.begin() + copy.bytes);
                server_offsets[level] += copy.bytes;
                if (!server.retire_output_prefix(static_cast<hd::quic_crypto_level>(level), server_offsets[level])) return false;
            }
        }
        return true;
    }
    bool connect() {
        for (unsigned turn = 0; turn < 2000; ++turn) {
            bool moved = false;
            if (!client.step() || !deliver_client()) return false;
            auto result = done ? server.process_post_handshake() : server.handshake();
            if (result.state == hd::tls_session::progress::failed || result.state == hd::tls_session::progress::eof) return false;
            if (result.state == hd::tls_session::progress::complete) done = true;
            if (!deliver_server(moved)) return false;
            if (done && SSL_is_init_finished(client.ssl.get()) && !moved && client.input[2].empty()) return true;
        }
        throw std::runtime_error("QUIC TLS test did not park");
    }
};
}  // namespace quic_test
#endif  // TEST_UNIT_QUIC_TLS_PEER_HPP_
