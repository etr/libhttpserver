/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "./tls_credentials_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
namespace {
// Independent SSL peer, verified against the explicit test root. No adapter
// client or shared server context; session caching is disabled for each peer.
struct connection {
    httpserver::manual_executor ex;
    hd::io_connection_owner owner{ex};
    hd::fake_io_backend raw;
    std::unique_ptr<hd::tls_io_backend> tls;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx{SSL_CTX_new(TLS_client_method()), SSL_CTX_free};
    std::unique_ptr<SSL, decltype(&SSL_free)> peer{nullptr, SSL_free};
    hd::tls_handshake_operation handshake{owner, 1};
    explicit connection(hd::tls_credentials_selection selection) {
        const auto root = tls_test::pem("data/tls_credentials/root.pem");
        std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(root.data(), static_cast<int>(root.size())), BIO_free);
        std::unique_ptr<X509, decltype(&X509_free)> cert(PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr), X509_free);
        if (!cert || X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx.get()), cert.get()) != 1) throw std::runtime_error("test root unavailable");
        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_OFF);
        peer.reset(SSL_new(ctx.get()));
        SSL_set_bio(peer.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_connect_state(peer.get());
        const unsigned char offers[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
        SSL_set_alpn_protos(peer.get(), offers, sizeof(offers));
        tls = std::make_unique<hd::tls_io_backend>(raw, ex, 1, std::move(selection), true);
    }
    ~connection() { close(); }
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
}  // namespace
LT_BEGIN_SUITE(tls_credentials_rotation_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_credentials_rotation_suite)
LT_BEGIN_AUTO_TEST(tls_credentials_rotation_suite, wire_rotation_pins_selected_and_established_generations)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    std::weak_ptr<const hd::tls_credentials_snapshot> retired = registry.acquire();
    connection established(registry.acquire()->select_default());
    LT_ASSERT(established.connect());
    LT_CHECK_EQ(established.serial(), std::int64_t{101});
    LT_CHECK_EQ(established.alpn(), "h2");
    auto delayed = registry.acquire()->select_default();
    auto replacement = tls_test::credentials("b");
    replacement.hosts[0].alpn = {"http/1.1"};
    LT_ASSERT(registry.replace(replacement).ok());
    connection selected(std::move(delayed));
    LT_ASSERT(selected.connect());
    LT_CHECK_EQ(selected.serial(), std::int64_t{101});
    LT_CHECK_EQ(selected.alpn(), "h2");
    LT_CHECK(!retired.expired());
    const std::string message = "old session survives replacement";
    std::array<std::byte, 100> incoming{};
    hd::read_operation read(established.owner, 1, incoming);
    read.submit(*established.tls);
    std::size_t written = 0;
    LT_ASSERT(SSL_write_ex(established.peer.get(), message.data(), message.size(), &written) == 1);
    established.drive();
    LT_ASSERT(read.state()->applied());
    LT_CHECK_EQ(read.state()->stored_result().transferred, message.size());
    LT_CHECK(std::equal(std::as_bytes(std::span(message)).begin(), std::as_bytes(std::span(message)).end(), incoming.begin()));
    connection fresh(registry.acquire()->select_default());
    LT_ASSERT(fresh.connect());
    LT_CHECK_EQ(fresh.serial(), std::int64_t{202});
    LT_CHECK_EQ(fresh.alpn(), "http/1.1");
    auto invalid = tls_test::credentials();
    invalid.hosts[0].private_key_pem = "invalid";
    LT_CHECK(!registry.replace(invalid).ok());
    LT_CHECK_EQ(ERR_peek_error(), 0UL);
    connection after_invalid(registry.acquire()->select_default());
    LT_ASSERT(after_invalid.connect());
    LT_CHECK_EQ(after_invalid.serial(), std::int64_t{202});
    LT_CHECK_EQ(after_invalid.alpn(), "http/1.1");
    selected.close();
    LT_CHECK(!retired.expired());
    established.close();
    LT_CHECK(retired.expired());
LT_END_AUTO_TEST(wire_rotation_pins_selected_and_established_generations)
LT_BEGIN_AUTO_TEST(tls_credentials_rotation_suite, retiring_adapter_work_keeps_snapshot_until_executor_drain)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    auto selected = registry.acquire()->select_default();
    std::weak_ptr<const hd::tls_credentials_snapshot> retired = selected.snapshot;
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    auto tls = std::make_unique<hd::tls_io_backend>(raw, ex, 1, std::move(selected), true);
    hd::tls_handshake_operation handshake(owner, 1);
    handshake.submit(*tls);
    ex.run_pending();
    LT_CHECK(!handshake.is_terminal());
    LT_CHECK(raw.pending_count() > 0);
    LT_ASSERT(registry.replace(tls_test::credentials("b")).ok());
    tls.reset();
    LT_CHECK(!retired.expired());
    ex.run_pending();
    LT_CHECK(handshake.state()->applied());
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
    LT_CHECK(retired.expired());
LT_END_AUTO_TEST(retiring_adapter_work_keeps_snapshot_until_executor_drain)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
