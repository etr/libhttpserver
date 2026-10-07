/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_TLS_MTLS_FIXTURE_HPP_
#define TEST_UNIT_TLS_MTLS_FIXTURE_HPP_
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <httpserver/detail/tls_session.hpp>
#include "./tls_credentials_fixture.hpp"
namespace hd = httpserver::detail;
namespace mtls_test {
using client_context = std::shared_ptr<SSL_CTX>;
using session_ptr = std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)>;
inline client_context client(int version) {
    client_context ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    const auto root = tls_test::pem("data/tls_credentials/root.pem");
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(root.data(), static_cast<int>(root.size())), BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!cert || X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx.get()), cert.get()) != 1) throw std::runtime_error("test root unavailable");
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_min_proto_version(ctx.get(), version);
    SSL_CTX_set_max_proto_version(ctx.get(), version);
    SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_CLIENT);
    return ctx;
}
inline void received(int writing, int, int type, const void* data, std::size_t size, SSL*, void* arg) {
    if (type != SSL3_RT_HANDSHAKE || size < 4) return;
    auto* counts = static_cast<std::array<unsigned, 2>*>(arg);
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (!writing && bytes[0] == SSL3_MT_CERTIFICATE_REQUEST) ++(*counts)[0];
    if (writing && bytes[0] == SSL3_MT_CERTIFICATE && size > 11) ++(*counts)[1];
}
inline client_context authenticated_client(int version, const char* identity, bool chain = true) {
    auto ctx = client(version);
    if (!identity) return ctx;
    const auto base = std::string(TLS_TEST_DIR) + "/data/tls_credentials/" + identity;
    if (SSL_CTX_use_certificate_file(ctx.get(), (base + ".pem").c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx.get(), (base + "-key.pem").c_str(), SSL_FILETYPE_PEM) != 1) throw std::runtime_error("client fixture unavailable");
    if (chain && std::string(identity) != "client-other") {
        const auto text = tls_test::pem("data/tls_credentials/client-intermediate.pem");
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(text.data(), static_cast<int>(text.size())), BIO_free);
        std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
        if (!cert || SSL_CTX_add1_chain_cert(ctx.get(), cert.get()) != 1) throw std::runtime_error("client chain unavailable");
    }
    return ctx;
}
inline hd::tls_credentials_config credentials(httpserver::server::tls_client_certificate_mode mode) {
    auto config = tls_test::credentials();
    config.hosts[0].client_auth.mode = mode;
    config.hosts[0].trust_roots_pem = tls_test::pem("data/tls_credentials/client-root.pem");
    config.hosts[0].alpn.clear();
    return config;
}
struct connection {
    hd::tls_session server;
    client_context ctx;
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    std::array<unsigned, 2> messages{};
    bool server_done = false;
    bool failed = false;
    connection(hd::tls_credentials_selection selected, client_context context, const char* name,
               const std::vector<std::string>& offers = {}, SSL_SESSION* ticket = nullptr)
        : server(std::move(selected), true), ctx(std::move(context)), peer(SSL_new(ctx.get()), SSL_free) {
        SSL_set_bio(peer.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_connect_state(peer.get());
        SSL_set_msg_callback(peer.get(), received);
        SSL_set_msg_callback_arg(peer.get(), &messages);
        if (name) SSL_set_tlsext_host_name(peer.get(), name);
        std::vector<unsigned char> wire;
        for (const auto& offer : offers) {
            wire.push_back(static_cast<unsigned char>(offer.size()));
            wire.insert(wire.end(), offer.begin(), offer.end());
        }
        if (!wire.empty()) SSL_set_alpn_protos(peer.get(), wire.data(), static_cast<unsigned>(wire.size()));
        if (ticket && SSL_set_session(peer.get(), ticket) != 1) throw std::runtime_error("test session unavailable");
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
    std::int64_t serial() const { return ASN1_INTEGER_get(X509_get_serialNumber(SSL_get0_peer_certificate(peer.get()))); }
    std::string alpn() const {
        const unsigned char* value = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(peer.get(), &value, &size);
        return size ? std::string(reinterpret_cast<const char*>(value), size) : std::string{};
    }
    session_ptr ticket() { return {SSL_get1_session(peer.get()), SSL_SESSION_free}; }
    bool reused() const { return SSL_session_reused(peer.get()) == 1; }
};
struct adapter_connection {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner{ex};
    hd::fake_io_backend raw;
    std::unique_ptr<hd::tls_io_backend> tls;
    mtls_test::client_context ctx;
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    hd::tls_handshake_operation handshake{owner, 1};
    explicit adapter_connection(hd::tls_credentials_selection selection, mtls_test::client_context context)
        : ctx(std::move(context)), peer(SSL_new(ctx.get()), SSL_free) {
        SSL_set_bio(peer.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_connect_state(peer.get());
        tls = std::make_unique<hd::tls_io_backend>(raw, ex, 1, std::move(selection), true);
    }
    ~adapter_connection() { close(); }
    void close() {
        if (tls) tls->close();
        ex.run_pending();
        tls.reset();
        ex.run_pending();
    }
    bool transfer(const std::shared_ptr<hd::op_state>& op) {
        if (op->kind() == hd::io_op_kind::write) {
            const auto bytes = std::get<hd::write_payload>(op->payload()).bytes;
            const auto count = std::min(bytes.size(), std::size_t{73});
            BIO_write(SSL_get_rbio(peer.get()), bytes.data(), static_cast<int>(count));
            raw.complete(*op, {httpserver::http::outcome_code::ok, count});
            return true;
        }
        if (op->kind() == hd::io_op_kind::read && BIO_ctrl_pending(SSL_get_wbio(peer.get())) > 0) {
            auto bytes = std::get<hd::read_payload>(op->payload()).buffer;
            const int count = BIO_read(SSL_get_wbio(peer.get()), bytes.data(), static_cast<int>(std::min(bytes.size(), std::size_t{41})));
            raw.complete(*op, {httpserver::http::outcome_code::ok, static_cast<std::size_t>(count)});
            return true;
        }
        return false;
    }
    bool peer_failed = false;
    void advance_peer() {
        if (peer_failed) return;
        if (SSL_is_init_finished(peer.get())) return;
        ERR_clear_error();
        const int rc = SSL_do_handshake(peer.get());
        if (rc == 1) return;
        const int error = SSL_get_error(peer.get(), rc);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) peer_failed = true;
    }
    void drive() {
        for (unsigned i = 0; i < 10000; ++i) {
            ex.run_pending();
            bool moved = false;
            for (const auto& op : raw.pending_ops()) moved = transfer(op) || moved;
            advance_peer();
            if (handshake.state()->applied() && handshake.state()->stored_result().code != httpserver::http::outcome_code::ok &&
                ex.pending() == 0 && raw.pending_count() == 0) return;
            if (!moved && ex.pending() == 0 && BIO_ctrl_pending(SSL_get_wbio(peer.get())) == 0) return;
        }
        throw std::runtime_error("rotation transport did not park");
    }
    bool connect() {
        handshake.submit(*tls);
        drive();
        return handshake.state()->applied() && handshake.state()->stored_result().code == httpserver::http::outcome_code::ok && SSL_is_init_finished(peer.get());
    }
    std::string alpn() const {
        const unsigned char* value = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(peer.get(), &value, &size);
        return size ? std::string(reinterpret_cast<const char*>(value), size) : std::string{};
    }
    std::int64_t serial() const { return ASN1_INTEGER_get(X509_get_serialNumber(SSL_get0_peer_certificate(peer.get()))); }
};
}  // namespace mtls_test
#endif  // TEST_UNIT_TLS_MTLS_FIXTURE_HPP_
