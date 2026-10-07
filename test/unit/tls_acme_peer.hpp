/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_TLS_ACME_PEER_HPP_
#define TEST_UNIT_TLS_ACME_PEER_HPP_
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/tls_session.hpp>
#include "./tls_acme_fixture.hpp"
namespace acme_test {
using client_context = std::shared_ptr<SSL_CTX>;
using session_ptr = std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)>;
inline client_context client(int version) {
    client_context ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_min_proto_version(ctx.get(), version);
    SSL_CTX_set_max_proto_version(ctx.get(), version);
    SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_CLIENT);
    return ctx;
}
inline unsigned u16(const unsigned char* p) { return (static_cast<unsigned>(p[0]) << 8) | p[1]; }
// Inspect received ServerHello (1.2) or EncryptedExtensions (1.3), rather
// than the peer's cached server name, to prove wire acknowledgement.
inline void received(int writing, int, int type, const void* data, std::size_t size, SSL*, void* arg) {
    if (writing || type != SSL3_RT_HANDSHAKE || size < 6) return;
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t pos = 4;
    if (bytes[0] == SSL3_MT_SERVER_HELLO) {
        if (size < 39) return;
        pos = 39 + bytes[38] + 3;
    } else if (bytes[0] != SSL3_MT_ENCRYPTED_EXTENSIONS) {
        return;
    }
    if (pos + 2 > size) return;
    pos += 2;
    while (pos + 4 <= size) {
        const auto extension = u16(bytes + pos);
        const auto length = u16(bytes + pos + 2);
        pos += 4;
        if (pos + length > size) return;
        if (extension == TLSEXT_TYPE_server_name) *static_cast<bool*>(arg) = true;
        pos += length;
    }
}
struct connection {
    hd::tls_session server;
    client_context ctx;
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    bool acknowledged = false;
    unsigned certificate_messages = 0;
    unsigned hello_retries = 0;
    bool server_done = false;
    bool failed = false;
    connection(hd::tls_credentials_selection selected, client_context context, const char* name,
               const std::vector<std::string>& offers = {}, SSL_SESSION* ticket = nullptr, hd::tls_handshake_context transport = {hd::tls_transport::tcp, 443})
        : server(std::move(selected), true, transport), ctx(std::move(context)), peer(SSL_new(ctx.get()), SSL_free) {
        SSL_set_bio(peer.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_connect_state(peer.get());
        SSL_set_msg_callback(peer.get(), observe);
        SSL_set_msg_callback_arg(peer.get(), this);
        if (name) SSL_set_tlsext_host_name(peer.get(), name);
        std::vector<unsigned char> wire;
        for (const auto& offer : offers) {
            wire.push_back(static_cast<unsigned char>(offer.size()));
            wire.insert(wire.end(), offer.begin(), offer.end());
        }
        if (!wire.empty()) SSL_set_alpn_protos(peer.get(), wire.data(), static_cast<unsigned>(wire.size()));
        if (ticket && SSL_set_session(peer.get(), ticket) != 1) throw std::runtime_error("test session unavailable");
    }
    static void observe(int writing, int version, int type, const void* data, std::size_t size, SSL* ssl, void* arg) {
        auto* self = static_cast<connection*>(arg);
        received(writing, version, type, data, size, ssl, &self->acknowledged);
        constexpr unsigned char retry_random[] = {0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
                                                  0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c};
        if (!writing && type == SSL3_RT_HANDSHAKE && size >= 38 && *static_cast<const unsigned char*>(data) == SSL3_MT_SERVER_HELLO &&
            std::memcmp(static_cast<const unsigned char*>(data) + 6, retry_random, sizeof(retry_random)) == 0) ++self->hello_retries;
        if (!writing && type == SSL3_RT_HANDSHAKE && size && *static_cast<const unsigned char*>(data) == SSL3_MT_CERTIFICATE) ++self->certificate_messages;
    }
    ~connection() { SSL_set_shutdown(peer.get(), SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN); }
    bool transfer() {
        std::array<std::byte, 73> bytes{};
        bool moved = false;
        const auto output = server.drain(bytes);
        if (output) {
            BIO_write(SSL_get_rbio(peer.get()), bytes.data(), static_cast<int>(output));
            moved = true;
        }
        const auto capacity = std::min(server.input_capacity(), std::size_t{41});
        if (capacity && BIO_ctrl_pending(SSL_get_wbio(peer.get()))) {
            const auto count = BIO_read(SSL_get_wbio(peer.get()), bytes.data(), static_cast<int>(capacity));
            if (!server.feed(std::span(bytes).first(count))) throw std::runtime_error("test feed failed");
            moved = true;
        }
        return moved;
    }
    bool connect() {
        for (unsigned i = 0; i < 10000; ++i) {
            if (!server_done && !failed) {
                const auto result = server.handshake();
                server_done = result.state == hd::tls_session::progress::complete;
                failed = result.state == hd::tls_session::progress::failed;
            }
            const bool moved = transfer();
            ERR_clear_error();
            int rc = 0;
            if (!SSL_is_init_finished(peer.get())) {
                rc = SSL_do_handshake(peer.get());
            } else {
                std::array<char, 16> bytes{};
                std::size_t count = 0;
                rc = SSL_read_ex(peer.get(), bytes.data(), bytes.size(), &count);
            }
            if (rc != 1) {
                const auto error = SSL_get_error(peer.get(), rc);
                if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return false;
            }
            if (!moved && !BIO_ctrl_pending(SSL_get_wbio(peer.get())) && !server.output_pending()) {
                return !failed && server_done && SSL_is_init_finished(peer.get());
            }
        }
        throw std::runtime_error("selection transport did not park");
    }
    void corrupt_name(unsigned mode) {
        SSL_do_handshake(peer.get());
        std::vector<unsigned char> hello(BIO_ctrl_pending(SSL_get_wbio(peer.get())));
        BIO_read(SSL_get_wbio(peer.get()), hello.data(), static_cast<int>(hello.size()));
        std::size_t pos = 43;
        pos += 1 + hello.at(pos);
        pos += 2 + u16(hello.data() + pos);
        pos += 1 + hello.at(pos);
        pos += 2;
        bool found = false;
        while (pos + 4 <= hello.size()) {
            const auto type = u16(hello.data() + pos);
            const auto size = u16(hello.data() + pos + 2);
            pos += 4;
            if (pos + size > hello.size()) throw std::runtime_error("test hello invalid");
            if (type == TLSEXT_TYPE_server_name) {
                if (mode == 0) hello.at(pos + 5) = 0;
                if (mode == 1) hello.at(pos + 1) ^= 1;
                if (mode == 2) hello.at(pos + 4) ^= 1;
                if (mode == 3) hello.at(pos + 2) = 1;
                if (mode == 4) {
                    const std::array<unsigned char, 12> duplicate{0, 0, 3, 'a', 'b', 'c', 0, 0, 3, 'd', 'e', 'f'};
                    std::copy(duplicate.begin(), duplicate.end(), hello.begin() + pos + 2);
                }
                found = true;
                break;
            }
            pos += size;
        }
        if (!found) throw std::runtime_error("test SNI missing");
        BIO_write(SSL_get_wbio(peer.get()), hello.data(), static_cast<int>(hello.size()));
    }
    std::int64_t serial() const { return ASN1_INTEGER_get(X509_get_serialNumber(SSL_get0_peer_certificate(peer.get()))); }
    void corrupt_alpn(unsigned mode) {
        SSL_do_handshake(peer.get());
        std::vector<unsigned char> hello(BIO_ctrl_pending(SSL_get_wbio(peer.get())));
        BIO_read(SSL_get_wbio(peer.get()), hello.data(), static_cast<int>(hello.size()));
        std::size_t pos = 43;
        pos += 1 + hello.at(pos);
        pos += 2 + u16(hello.data() + pos);
        pos += 1 + hello.at(pos);
        pos += 2;
        bool found = false;
        while (pos + 4 <= hello.size()) {
            const auto type = u16(hello.data() + pos);
            const auto size = u16(hello.data() + pos + 2);
            pos += 4;
            if (type == TLSEXT_TYPE_application_layer_protocol_negotiation) {
                if (mode == 0) hello.at(pos + 1) ^= 1;
                if (mode == 1) hello.at(pos + 2) = 0;
                if (mode == 2) hello.at(pos + 2) = 11;
                if (mode == 3) hello.at(pos + 2) = 9;
                found = true;
            }
            pos += size;
        }
        if (!found) throw std::runtime_error("test ALPN missing");
        BIO_write(SSL_get_wbio(peer.get()), hello.data(), static_cast<int>(hello.size()));
    }
    bool digest_matches(const hd::tls_acme_challenge& challenge) const {
        auto* cert = SSL_get0_peer_certificate(peer.get());
        if (!cert) return false;
        const int index = acme_index(cert);
        if (index < 0) return false;
        auto* ext = X509_get_ext(cert, index);
        auto* data = X509_EXTENSION_get_data(ext);
        return X509_EXTENSION_get_critical(ext) && ASN1_STRING_length(data) == 34 &&
            std::memcmp(ASN1_STRING_get0_data(data) + 2, challenge.key_authorization_sha256.data(), 32) == 0;
    }
    std::string alpn() const {
        const unsigned char* value = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(peer.get(), &value, &size);
        return size ? std::string(reinterpret_cast<const char*>(value), size) : std::string{};
    }
    session_ptr ticket() { return {SSL_get1_session(peer.get()), SSL_SESSION_free}; }
    bool reused() const { return SSL_session_reused(peer.get()) == 1; }
};
}  // namespace acme_test
#endif  // TEST_UNIT_TLS_ACME_PEER_HPP_
