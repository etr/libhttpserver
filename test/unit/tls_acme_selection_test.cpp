/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "./tls_acme_peer.hpp"
#include "./tls_psk_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
LT_BEGIN_SUITE(tls_acme_selection_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_acme_selection_suite)
LT_BEGIN_AUTO_TEST(tls_acme_selection_suite, exact_tcp443_sole_alpn_selects_challenge_and_blocks_application_io)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    const auto input = acme_test::challenge();
    LT_ASSERT(registry.publish_acme(input).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge("new.example", 404)).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const auto* name : {"a.example", "A.Example.", "new.example"}) {
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), name, {"acme-tls/1"});
            LT_ASSERT(peer.connect());
            LT_CHECK_EQ(peer.serial(), std::string(name) == "new.example" ? std::int64_t{404} : std::int64_t{303});
            LT_CHECK_EQ(peer.alpn(), "acme-tls/1");
            LT_CHECK(peer.digest_matches(input));
            LT_CHECK_EQ(peer.certificate_messages, 1U);
            LT_CHECK(peer.acknowledged);
            std::array<std::byte, 16> bytes{};
            const auto read = peer.server.read(bytes), write = peer.server.write(bytes);
            LT_CHECK(read.state == hd::tls_session::progress::failed);
            LT_CHECK(write.state == hd::tls_session::progress::failed);
            LT_CHECK_EQ(read.bytes + write.bytes, std::size_t{0});
            LT_CHECK(read.failure == httpserver::http::outcome_code::protocol_error);
        }
        for (const auto& transport : {hd::tls_handshake_context{}, hd::tls_handshake_context{hd::tls_transport::tcp, 0},
                                     hd::tls_handshake_context{hd::tls_transport::tcp, 8443}, hd::tls_handshake_context{hd::tls_transport::quic, 443}}) {
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"}, nullptr, transport);
            LT_CHECK(!peer.connect());
            LT_CHECK(SSL_get0_peer_certificate(peer.peer.get()) == nullptr);
        }
        for (const char* name : {static_cast<const char*>(nullptr), "unknown.example", "other.a.example", "a.example.evil"}) {
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), name, {"acme-tls/1"});
            LT_CHECK(!peer.connect());
            LT_CHECK(SSL_get0_peer_certificate(peer.peer.get()) == nullptr);
        }
        for (const auto& offer : std::vector<std::vector<std::string>>{{}, {"h2"}, {"http/1.1"}, {"acme-tls/1", "h2"}, {"h2", "acme-tls/1"}}) {
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", offer);
            LT_ASSERT(peer.connect());
            LT_CHECK_EQ(peer.serial(), std::int64_t{101});
            LT_CHECK(!peer.digest_matches(input));
            LT_CHECK_EQ(peer.alpn(), offer.empty() ? "" : (offer[0] == "http/1.1" ? "http/1.1" : "h2"));
        }
        for (const auto& offers : std::vector<std::vector<std::string>>{{"acme-tls/1", "acme-tls/1"}, {"acme-tls/2"}}) {
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", offers);
            LT_CHECK(!peer.connect());
            LT_CHECK(SSL_get0_peer_certificate(peer.peer.get()) == nullptr);
        }
        for (unsigned mode = 0; mode < 4; ++mode) {
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
            peer.corrupt_alpn(mode);
            LT_CHECK(!peer.connect());
            LT_CHECK(SSL_get0_peer_certificate(peer.peer.get()) == nullptr);
        }
        for (unsigned mode = 0; mode < 5; ++mode) {
            acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
            peer.corrupt_name(mode);
            LT_CHECK(!peer.connect());
        }
    }
    LT_ASSERT(registry.remove_acme("a.example").ok());
    auto config = tls_test::credentials(); config.hosts[0].alpn.clear();
    LT_ASSERT(registry.replace(config).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
        LT_CHECK(!peer.connect());
        LT_CHECK(SSL_get0_peer_certificate(peer.peer.get()) == nullptr);
    }
LT_END_AUTO_TEST(exact_tcp443_sole_alpn_selects_challenge_and_blocks_application_io)
LT_BEGIN_AUTO_TEST(tls_acme_selection_suite, challenge_requires_fresh_certificate_even_when_normal_or_challenge_session_is_offered)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(tls_test::credentials()).ok());
        LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
        auto ctx = acme_test::client(version);
        acme_test::connection normal(registry.acquire()->select_default(), ctx, "a.example", {"h2"});
        LT_ASSERT(normal.connect());
        auto ticket = normal.ticket();
        LT_ASSERT(ticket && SSL_SESSION_is_resumable(ticket.get()));
        acme_test::connection first(registry.acquire()->select_default(), ctx, "a.example", {"acme-tls/1"}, ticket.get());
        LT_ASSERT(first.connect());
        LT_CHECK(!first.reused());
        LT_CHECK_EQ(first.certificate_messages, 1U);
        LT_CHECK_EQ(first.serial(), std::int64_t{303});
        auto challenge_ticket = first.ticket();
        LT_ASSERT(challenge_ticket);
        LT_CHECK(!SSL_SESSION_has_ticket(challenge_ticket.get()));
        const auto replacement = acme_test::challenge("a.example", 404, {}, std::byte{0x43});
        LT_ASSERT(registry.publish_acme(replacement).ok());
        acme_test::connection second(registry.acquire()->select_default(), ctx, "a.example", {"acme-tls/1"}, challenge_ticket.get());
        LT_ASSERT(second.connect());
        LT_CHECK(!second.reused());
        LT_CHECK_EQ(second.certificate_messages, 1U);
        LT_CHECK_EQ(second.serial(), std::int64_t{404});
        LT_CHECK(second.digest_matches(replacement));
        LT_ASSERT(registry.remove_acme("a.example").ok());
        acme_test::connection removed(registry.acquire()->select_default(), ctx, "a.example", {"acme-tls/1"}, challenge_ticket.get());
        LT_CHECK(!removed.connect());
    }
LT_END_AUTO_TEST(challenge_requires_fresh_certificate_even_when_normal_or_challenge_session_is_offered)
LT_BEGIN_AUTO_TEST(tls_acme_selection_suite, ordinary_mutual_tls_authentication_does_not_leak_into_challenge)
    hd::tls_credentials_registry registry;
    auto config = tls_test::credentials();
    config.hosts[0].profile = httpserver::server::tls_profile::mutual_tls;
    LT_ASSERT(registry.replace(config).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
        LT_ASSERT(peer.connect());
        LT_CHECK_EQ(peer.serial(), std::int64_t{303});
        LT_CHECK(!peer.server.peer_metadata()->has_client_certificate);
    }
LT_END_AUTO_TEST(ordinary_mutual_tls_authentication_does_not_leak_into_challenge)
LT_BEGIN_AUTO_TEST(tls_acme_selection_suite, challenge_clears_external_psk_callbacks_from_initial_context)
    auto runtime = std::make_shared<hd::tls_psk_runtime>();
    unsigned lookups = 0;
    psk_test::client_key identity{"test-acme", std::vector<std::byte>(32, std::byte{0x23})};
    auto config = psk_test::credentials(runtime, [&](std::span<const std::byte>, const hd::psk_handshake_context&) {
        ++lookups;
        return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(identity.key)};
    });
    config.hosts.push_back(tls_test::credentials().hosts[0]);
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(config).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto ctx = acme_test::client(version);
        SSL_CTX_set_cipher_list(ctx.get(), "DEFAULT:PSK-AES128-GCM-SHA256");
        SSL_CTX_set_psk_client_callback(ctx.get(), psk_test::client_key::legacy);
        SSL_CTX_set_psk_use_session_callback(ctx.get(), psk_test::client_key::session);
        acme_test::connection peer(registry.acquire()->select_default(), ctx, "a.example", {"acme-tls/1"});
        SSL_set_app_data(peer.peer.get(), &identity);
        LT_ASSERT(peer.connect());
        LT_CHECK_EQ(peer.serial(), std::int64_t{303});
        LT_CHECK_EQ(peer.alpn(), "acme-tls/1");
    }
    LT_CHECK_EQ(lookups, 0U);
LT_END_AUTO_TEST(challenge_clears_external_psk_callbacks_from_initial_context)
LT_BEGIN_AUTO_TEST(tls_acme_selection_suite, snapshotless_sessions_cannot_obtain_challenges_and_challenge_shutdown_remains_usable)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto selected = registry.acquire()->select_default();
        selected.snapshot.reset();
        acme_test::connection missing(std::move(selected), acme_test::client(version), "a.example", {"acme-tls/1"});
        LT_CHECK(!missing.connect());
        LT_CHECK(SSL_get0_peer_certificate(missing.peer.get()) == nullptr);
        acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
        LT_ASSERT(peer.connect());
        LT_CHECK(peer.server.shutdown().state == hd::tls_session::progress::input);
        for (unsigned i = 0; i < 100 && peer.transfer(); ++i) {}
        std::array<std::byte, 16> bytes{};
        std::size_t count = 0;
        ERR_clear_error();
        const auto rc = SSL_read_ex(peer.peer.get(), bytes.data(), bytes.size(), &count);
        LT_CHECK(SSL_get_error(peer.peer.get(), rc) == SSL_ERROR_ZERO_RETURN);
        LT_CHECK(SSL_shutdown(peer.peer.get()) == 1);
        for (unsigned i = 0; i < 100 && peer.transfer(); ++i) {}
        LT_CHECK(peer.server.shutdown().state == hd::tls_session::progress::complete);
    }
LT_END_AUTO_TEST(snapshotless_sessions_cannot_obtain_challenges_and_challenge_shutdown_remains_usable)
LT_BEGIN_AUTO_TEST(tls_acme_selection_suite, selected_challenge_remains_pinned_through_hello_retry_even_after_deadline)
    auto challenge = acme_test::challenge();
    auto selection_time = challenge.expires_at - std::chrono::seconds(1);
    hd::tls_credentials_registry registry([&] { return selection_time; });
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    LT_ASSERT(registry.publish_acme(challenge).ok());
    const auto snapshot = registry.acquire();
    LT_ASSERT(snapshot->select_acme("a.example").has_value());
    acme_test::connection peer(registry.acquire()->select_default(), acme_test::client(TLS1_3_VERSION), "a.example", {"acme-tls/1"});
    // Predict only X25519; the provider's stronger first group requires a retry.
    LT_ASSERT(SSL_set1_groups_list(peer.peer.get(), "X25519MLKEM768:*X25519") == 1);
    for (unsigned i = 0; i < 10000 && peer.hello_retries == 0; ++i) {
        const auto status = peer.server.handshake();
        LT_ASSERT(status.state != hd::tls_session::progress::failed);
        peer.transfer();
        ERR_clear_error();
        const auto rc = SSL_do_handshake(peer.peer.get());
        const auto error = rc == 1 ? SSL_ERROR_NONE : SSL_get_error(peer.peer.get(), rc);
        LT_ASSERT(error == SSL_ERROR_NONE || error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
    }
    LT_ASSERT(peer.hello_retries == 1);
    selection_time = challenge.expires_at;
    LT_CHECK(!snapshot->select_acme("a.example"));
    acme_test::connection expired(registry.acquire()->select_default(), acme_test::client(TLS1_3_VERSION), "a.example", {"acme-tls/1"});
    LT_CHECK(!expired.connect());
    LT_CHECK(SSL_get0_peer_certificate(expired.peer.get()) == nullptr);
    LT_ASSERT(peer.connect());
    LT_CHECK_EQ(peer.serial(), std::int64_t{303});
    LT_CHECK_EQ(peer.alpn(), "acme-tls/1");
    LT_CHECK_EQ(peer.certificate_messages, 1U);
    LT_CHECK(peer.digest_matches(challenge));
LT_END_AUTO_TEST(selected_challenge_remains_pinned_through_hello_retry_even_after_deadline)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
