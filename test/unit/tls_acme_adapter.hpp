/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_TLS_ACME_ADAPTER_HPP_
#define TEST_UNIT_TLS_ACME_ADAPTER_HPP_
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "./tls_acme_fixture.hpp"
namespace acme_test {
namespace hh = httpserver::http;
struct adapter_connection {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner{ex};
    hd::fake_io_backend raw;
    std::unique_ptr<hd::tls_io_backend> tls;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx{SSL_CTX_new(TLS_client_method()), SSL_CTX_free};
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    hd::tls_handshake_operation handshake{owner, 1};
    explicit adapter_connection(hd::tls_credentials_selection selection, bool challenge = true) {
        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
        SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_OFF);
        peer.reset(SSL_new(ctx.get()));
        SSL_set_bio(peer.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_connect_state(peer.get());
        SSL_set_tlsext_host_name(peer.get(), "a.example");
        const std::string token = challenge ? "acme-tls/1" : "h2";
        std::string offers(1, static_cast<char>(token.size()));
        offers += token;
        SSL_set_alpn_protos(peer.get(), reinterpret_cast<const unsigned char*>(offers.data()), static_cast<unsigned>(offers.size()));
        tls = std::make_unique<hd::tls_io_backend>(raw, ex, 1, std::move(selection), true, hd::tls_handshake_context{hd::tls_transport::tcp, 443});
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
            raw.complete(*op, {hh::outcome_code::ok, count});
            return true;
        }
        if (op->kind() == hd::io_op_kind::read && BIO_ctrl_pending(SSL_get_wbio(peer.get())) > 0) {
            auto bytes = std::get<hd::read_payload>(op->payload()).buffer;
            const int count = BIO_read(SSL_get_wbio(peer.get()), bytes.data(), static_cast<int>(std::min(bytes.size(), std::size_t{41})));
            raw.complete(*op, {hh::outcome_code::ok, static_cast<std::size_t>(count)});
            return true;
        }
        return false;
    }
    void advance_peer() {
        if (SSL_is_init_finished(peer.get())) return;
        ERR_clear_error();
        const int rc = SSL_do_handshake(peer.get());
        if (rc == 1) return;
        const int error = SSL_get_error(peer.get(), rc);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) throw std::runtime_error("verified peer handshake failed");
    }
    void drive() {
        for (unsigned i = 0; i < 10000; ++i) {
            ex.run_pending();
            bool moved = false;
            for (const auto& op : raw.pending_ops()) moved = transfer(op) || moved;
            advance_peer();
            if (!moved && ex.pending() == 0 && BIO_ctrl_pending(SSL_get_wbio(peer.get())) == 0) return;
        }
        throw std::runtime_error("rotation transport did not park");
    }
    bool connect() {
        handshake.submit(*tls);
        drive();
        return handshake.state()->applied() && handshake.state()->stored_result().code == hh::outcome_code::ok && SSL_is_init_finished(peer.get());
    }
    std::string alpn() const {
        const unsigned char* value = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(peer.get(), &value, &size);
        return size ? std::string(reinterpret_cast<const char*>(value), size) : std::string{};
    }
    std::int64_t serial() const { return ASN1_INTEGER_get(X509_get_serialNumber(SSL_get0_peer_certificate(peer.get()))); }
};
}  // namespace acme_test
#endif  // TEST_UNIT_TLS_ACME_ADAPTER_HPP_
