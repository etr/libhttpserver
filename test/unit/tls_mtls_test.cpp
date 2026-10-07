/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include "./tls_mtls_fixture.hpp"
#include "./littletest.hpp"
namespace srv = httpserver::server;
using mode = srv::tls_client_certificate_mode;
using mtls_test::connection;
using mtls_test::authenticated_client;
LT_BEGIN_SUITE(tls_mtls_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_mtls_suite)
LT_BEGIN_AUTO_TEST(tls_mtls_suite, initial_handshake_modes_and_presented_invalid_chains)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (auto policy : {mode::none, mode::request, mode::require}) {
            hd::tls_credentials_registry registry;
            LT_ASSERT(registry.replace(mtls_test::credentials(policy)).ok());
            for (const char* identity : {static_cast<const char*>(nullptr), "client-valid", "client-other", "client-expired", "client-wrong-purpose"}) {
                connection peer(registry.acquire()->select_default(), authenticated_client(version, identity), "a.example");
                LT_CHECK(!peer.server.peer_metadata());
                const bool valid = policy == mode::none || (identity ? std::string(identity) == "client-valid" : policy == mode::request);
                LT_CHECK_EQ(peer.connect(), valid);
                LT_CHECK_EQ(peer.messages[0], policy == mode::none ? 0u : 1u);
                if (policy == mode::none) LT_CHECK_EQ(peer.messages[1], 0u);
                if (policy != mode::none && identity) LT_CHECK_EQ(peer.messages[1], 1u);
                const auto metadata = peer.server.peer_metadata();
                if (!valid) {
                    LT_CHECK(!metadata);
                    continue;
                }
                LT_CHECK(static_cast<bool>(metadata));
                if (!metadata) continue;
                LT_CHECK_EQ(metadata->has_client_certificate, identity && policy != mode::none);
                LT_CHECK_EQ(metadata->client_certificate_verified, identity && policy != mode::none);
                if (!metadata->has_client_certificate) {
                    LT_CHECK(metadata->subject_dn.empty());
                    LT_CHECK(metadata->fingerprint_sha256.empty());
                    LT_CHECK_EQ(metadata->not_before, -1L);
                }
            }
            if (policy != mode::none) {
                connection missing(registry.acquire()->select_default(), authenticated_client(version, "client-valid", false), "a.example");
                LT_CHECK(!missing.connect());
                LT_CHECK_EQ(missing.messages[1], 1u);
                LT_CHECK(!missing.server.peer_metadata());
            }
        }
    }
LT_END_AUTO_TEST(initial_handshake_modes_and_presented_invalid_chains)
LT_BEGIN_AUTO_TEST(tls_mtls_suite, selected_host_applies_mode_and_trust_store)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (auto initial : {mode::none, mode::require}) {
            auto config = mtls_test::credentials(initial);
            config.hosts.push_back(tls_test::credentials("b").hosts[0]);
            config.hosts[1].alpn.clear();
            config.hosts[1].client_auth.mode = initial == mode::none ? mode::require : mode::request;
            config.hosts[1].trust_roots_pem = tls_test::pem("data/tls_credentials/client-other-root.pem");
            hd::tls_credentials_registry registry;
            LT_ASSERT(registry.replace(config).ok());
            connection trusted(registry.acquire()->select_default(), authenticated_client(version, "client-other"), "b.example");
            LT_CHECK(trusted.connect());
            connection untrusted(registry.acquire()->select_default(), authenticated_client(version, "client-valid"), "b.example");
            LT_CHECK(!untrusted.connect());
            connection absent(registry.acquire()->select_default(), authenticated_client(version, nullptr), "b.example");
            LT_CHECK_EQ(absent.connect(), initial != mode::none);
            for (const char* name : {"unknown.example", static_cast<const char*>(nullptr)}) {
                connection fallback(registry.acquire()->select_default(), authenticated_client(version, nullptr), name);
                LT_CHECK_EQ(fallback.connect(), initial == mode::none);
            }
            config.hosts[1].client_auth.mode = mode::none;
            LT_ASSERT(registry.replace(config).ok());
            connection disabled(registry.acquire()->select_default(), authenticated_client(version, nullptr), "b.example");
            LT_CHECK(disabled.connect());
            LT_CHECK_EQ(disabled.messages[0], 0u);
        }
    }
LT_END_AUTO_TEST(selected_host_applies_mode_and_trust_store)
LT_BEGIN_AUTO_TEST(tls_mtls_suite, peer_identity_is_copied_and_resumption_pins_authentication)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        hd::tls_credentials_registry registry;
        auto config = mtls_test::credentials(mode::require);
        LT_ASSERT(registry.replace(config).ok());
        const auto delayed = registry.acquire()->select_default();
        auto ctx = authenticated_client(version, "client-valid");
        connection initial(delayed, ctx, "a.example");
        LT_ASSERT(initial.connect());
        const auto identity = initial.server.peer_metadata();
        LT_ASSERT(identity);
        LT_CHECK_EQ(identity->subject_dn, "O=Test Clients,CN=Alice");
        LT_CHECK_EQ(identity->issuer_dn, "CN=Client Intermediate");
        LT_CHECK_EQ(identity->common_name, "Alice");
        LT_CHECK_EQ(identity->fingerprint_sha256, "ea03058b57d2a2090d4e26f6a320d8c18b03fea4b0d7c90601eebe15d0cee7ab");
        LT_CHECK_EQ(identity->not_before, 1577836800L);
        LT_CHECK_EQ(identity->not_after, 2208988800L);
        auto ticket = initial.ticket();
        LT_ASSERT(SSL_SESSION_is_resumable(ticket.get()) == 1);
        connection resumed(delayed, ctx, "a.example", {}, ticket.get());
        SSL_certs_clear(resumed.peer.get());
        LT_ASSERT(resumed.connect());
        LT_CHECK(resumed.reused());
        LT_CHECK_EQ(resumed.messages[0], 0u);
        LT_ASSERT(resumed.server.peer_metadata());
        LT_CHECK(resumed.server.peer_metadata()->client_certificate_verified);
        LT_CHECK_EQ(resumed.server.peer_metadata()->fingerprint_sha256, identity->fingerprint_sha256);
        auto fresh_ticket = resumed.ticket();
        config.hosts.push_back(tls_test::credentials("b").hosts[0]);
        config.hosts[1].client_auth.mode = mode::require;
        config.hosts[1].trust_roots_pem = tls_test::pem("data/tls_credentials/client-other-root.pem");
        config.hosts[1].alpn.clear();
        LT_ASSERT(registry.replace(config).ok());
        connection changed(registry.acquire()->select_default(), ctx, "b.example", {}, fresh_ticket.get());
        LT_CHECK(!changed.connect());
        LT_CHECK(!changed.reused());
        LT_CHECK(!changed.server.peer_metadata());
        config.hosts[0].trust_roots_pem = config.hosts[1].trust_roots_pem;
        LT_ASSERT(registry.replace(config).ok());
        connection retained(delayed, ctx, "a.example");
        LT_CHECK(retained.connect());
        connection replaced(registry.acquire()->select_default(), ctx, "a.example", {}, fresh_ticket.get());
        LT_CHECK(!replaced.connect());
        LT_CHECK(!replaced.reused());
        LT_CHECK(!replaced.server.peer_metadata());
        connection no_cn(delayed, authenticated_client(version, "client-no-cn"), "a.example");
        LT_ASSERT(no_cn.connect());
        LT_ASSERT(no_cn.server.peer_metadata());
        LT_CHECK(no_cn.server.peer_metadata()->common_name.empty());
    }
LT_END_AUTO_TEST(peer_identity_is_copied_and_resumption_pins_authentication)
LT_BEGIN_AUTO_TEST(tls_mtls_suite, host_ticket_cannot_bypass_another_hosts_authentication)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto config = mtls_test::credentials(mode::none);
        config.hosts.push_back(tls_test::credentials("b").hosts[0]);
        config.hosts[1].alpn.clear();
        config.hosts[1].client_auth.mode = mode::require;
        config.hosts[1].trust_roots_pem = config.hosts[0].trust_roots_pem;
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        auto ctx = authenticated_client(version, nullptr);
        connection initial(registry.acquire()->select_default(), ctx, "a.example");
        LT_ASSERT(initial.connect());
        auto ticket = initial.ticket();
        LT_ASSERT(SSL_SESSION_is_resumable(ticket.get()) == 1);
        connection changed(registry.acquire()->select_default(), ctx, "b.example", {}, ticket.get());
        LT_CHECK(!changed.connect());
        LT_CHECK(!changed.reused());
        LT_CHECK(!changed.server.peer_metadata());
    }
LT_END_AUTO_TEST(host_ticket_cannot_bypass_another_hosts_authentication)
LT_BEGIN_AUTO_TEST(tls_mtls_suite, adapter_metadata_publication_lifetime_and_authentication_failure)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(mtls_test::credentials(mode::require)).ok());
        std::shared_ptr<const srv::tls_peer_metadata> retained;
        {
            mtls_test::adapter_connection peer(registry.acquire()->select_default(), authenticated_client(version, "client-valid"));
            LT_CHECK(!peer.tls->peer_metadata());
            std::atomic<bool> stop{false};
            std::atomic<unsigned> errors{0};
            std::thread reader([&] {
                while (!stop.load()) {
                    const auto metadata = peer.tls->peer_metadata();
                    if (metadata && (!metadata->client_certificate_verified || metadata->common_name != "Alice")) ++errors;
                    std::this_thread::yield();
                }
            });
            const bool connected = peer.connect();
            stop = true;
            reader.join();
            LT_CHECK(connected);
            LT_CHECK_EQ(errors.load(), 0u);
            retained = peer.tls->peer_metadata();
            LT_CHECK(static_cast<bool>(retained));
            LT_ASSERT(registry.replace(mtls_test::credentials(mode::none)).ok());
        }
        LT_ASSERT(retained);
        LT_CHECK(retained->client_certificate_verified);
        LT_CHECK_EQ(retained->common_name, "Alice");
        LT_ASSERT(registry.replace(mtls_test::credentials(mode::require)).ok());
        mtls_test::adapter_connection rejected(registry.acquire()->select_default(), authenticated_client(version, "client-other"));
        LT_CHECK(!rejected.connect());
        LT_ASSERT(rejected.handshake.state()->applied());
        LT_CHECK(rejected.handshake.state()->stored_result().code == httpserver::http::outcome_code::protocol_error);
        LT_CHECK(!rejected.tls->peer_metadata());
        LT_CHECK_EQ(rejected.raw.pending_count(), std::size_t{0});
        rejected.tls->close();
        rejected.ex.run_pending();
        LT_CHECK(!rejected.handshake.state()->claim_terminal());
        LT_CHECK(rejected.handshake.state()->stored_result().code == httpserver::http::outcome_code::protocol_error);
    }
LT_END_AUTO_TEST(adapter_metadata_publication_lifetime_and_authentication_failure)
LT_BEGIN_AUTO_TEST(tls_mtls_suite, default_mutual_and_snapshotless_contexts_enforce_authentication)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto config = mtls_test::credentials(mode::require);
        config.hosts[0].profile = srv::tls_profile::mutual_tls;
        config.hosts[0].client_auth.mode.reset();
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        connection missing(registry.acquire()->select_default(), authenticated_client(version, nullptr), "a.example");
        LT_CHECK(!missing.connect());
        connection authenticated(registry.acquire()->select_default(), authenticated_client(version, "client-valid"), "a.example");
        LT_CHECK(authenticated.connect());
        const auto& host = config.hosts[0];
        auto context = hd::tls_context::server_pem(host.certificate_chain_pem, host.private_key_pem, host.trust_roots_pem, mode::require);
        connection direct({nullptr, context}, authenticated_client(version, "client-valid"), nullptr);
        LT_ASSERT(direct.connect());
        LT_ASSERT(direct.server.peer_metadata());
        LT_CHECK(direct.server.peer_metadata()->client_certificate_verified);
        connection untrusted({nullptr, context}, authenticated_client(version, "client-other"), nullptr);
        LT_CHECK(!untrusted.connect());
        LT_CHECK(!untrusted.server.peer_metadata());
        connection anonymous({nullptr, context}, authenticated_client(version, nullptr), nullptr);
        LT_CHECK(!anonymous.connect());
        LT_CHECK_THROW(hd::tls_context::server_pem(host.certificate_chain_pem, host.private_key_pem, {}, mode::request));
    }
LT_END_AUTO_TEST(default_mutual_and_snapshotless_contexts_enforce_authentication)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
