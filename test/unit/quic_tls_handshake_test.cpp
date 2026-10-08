/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <httpserver/detail/quic_tls_session.hpp>
#include "./tls_credentials_fixture.hpp"
#include "./tls_acme_fixture.hpp"
#include "./tls_psk_fixture.hpp"
#include "./littletest.hpp"
#include "./quic_tls_peer.hpp"
LT_BEGIN_SUITE(quic_tls_handshake_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(quic_tls_handshake_suite)
LT_BEGIN_AUTO_TEST(quic_tls_handshake_suite, verified_fragmented_handshake_negotiates_h3_and_installs_matching_secrets)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(quic_test::credentials()).ok());
    quic_test::connection connection(registry.acquire()->select_default());
    LT_ASSERT(connection.connect());
    LT_CHECK(connection.client.alpn() == "h3");
    LT_CHECK(SSL_get_verify_result(connection.client.ssl.get()) == X509_V_OK);
    LT_CHECK(SSL_version(connection.client.ssl.get()) == TLS1_3_VERSION);
    LT_CHECK(connection.server.negotiated_protocol() == hd::tls_negotiated_protocol::h3);
    for (auto level : {hd::quic_key_level::handshake, hd::quic_key_level::application}) {
        for (auto direction : {hd::quic_key_direction::read, hd::quic_key_direction::write}) {
            const auto* keys = connection.keys.keys(level, direction);
            LT_ASSERT(keys);
            const auto& peer = connection.client.secrets[static_cast<unsigned>(level)][direction == hd::quic_key_direction::read ? 1 : 0];
            LT_CHECK(std::equal(peer.begin(), peer.end(), keys->secret.bytes().begin(), keys->secret.bytes().end()));
        }
    }
    LT_CHECK(connection.server.peer_transport_parameter_bytes().size() == quic_test::client_parameters.size());
    LT_CHECK(connection.client.parameters == std::vector<std::byte>(quic_test::server_parameters.begin(), quic_test::server_parameters.end()));
    LT_CHECK(connection.client.early_secrets == 0);
LT_END_AUTO_TEST(verified_fragmented_handshake_negotiates_h3_and_installs_matching_secrets)
LT_BEGIN_AUTO_TEST(quic_tls_handshake_suite, sni_policy_rotation_and_missing_h3_fail_closed)
    hd::tls_credentials_registry registry;
    auto config = quic_test::credentials();
    config.hosts.push_back(tls_test::credentials("b").hosts[0]);
    config.hosts[1].alpn = {"h3"};
    LT_ASSERT(registry.replace(config).ok());
    quic_test::connection old(registry.acquire()->select_default());
    old.client.step();
    LT_ASSERT(old.deliver_client(1));
    old.server.handshake();
    auto rotated = config;
    rotated.hosts[0] = config.hosts[1];
    rotated.hosts[0].host = "a.example";
    LT_ASSERT(registry.replace(rotated).ok());
    LT_ASSERT(old.connect());
    LT_CHECK(old.client.serial() == 101);
    quic_test::connection current(registry.acquire()->select_default());
    LT_ASSERT(SSL_set1_host(current.client.ssl.get(), "b.example"));
    LT_ASSERT(current.connect());
    LT_CHECK(current.client.serial() == 202);
    quic_test::connection b(registry.acquire()->select_default(), quic_test::client_context(), "b.example");
    LT_ASSERT(b.connect());
    LT_CHECK(b.client.serial() == 202);
    for (const auto& offers : std::vector<std::vector<std::string>>{{}, {"h2"}, {"acme-tls/1"}}) {
        quic_test::connection rejected(registry.acquire()->select_default(), quic_test::client_context(), "a.example", offers);
        LT_CHECK(!rejected.connect());
        LT_CHECK(rejected.server.negotiated_protocol() == hd::tls_negotiated_protocol::unknown);
        LT_CHECK(rejected.server.failure().code != hd::quic_tls_code::ok);
    }
    config.hosts[0].alpn = {"h2"};
    LT_ASSERT(registry.replace(config).ok());
    quic_test::connection policy(registry.acquire()->select_default());
    LT_CHECK(!policy.connect());
LT_END_AUTO_TEST(sni_policy_rotation_and_missing_h3_fail_closed)
LT_BEGIN_AUTO_TEST(quic_tls_handshake_suite, post_handshake_tickets_resume_with_early_data_disabled_and_explicit_offer_rejected)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(quic_test::credentials()).ok());
    auto ctx = quic_test::client_context();
    quic_test::connection first(registry.acquire()->select_default(), ctx);
    LT_ASSERT(first.connect());
    auto ticket = first.client.ticket();
    LT_ASSERT(ticket && SSL_SESSION_is_resumable(ticket.get()));
    LT_CHECK(SSL_SESSION_has_ticket(ticket.get()));
    LT_CHECK(SSL_SESSION_get_max_early_data(ticket.get()) == 0);
    quic_test::connection resumed(registry.acquire()->select_default(), ctx, "a.example", {"h3"}, ticket.get());
    LT_ASSERT(resumed.connect());
    LT_CHECK(SSL_session_reused(resumed.client.ssl.get()) == 1);
    LT_CHECK(resumed.client.early_secrets == 0);
    auto modified = resumed.client.ticket();
    LT_ASSERT(modified && SSL_SESSION_set_max_early_data(modified.get(), 0xffffffffU));
    quic_test::connection early(registry.acquire()->select_default(), ctx, "a.example", {"h3"}, modified.get(), true);
    LT_ASSERT(early.connect());
    LT_CHECK(early.client.offered_early_data);
    LT_CHECK(SSL_get_early_data_status(early.client.ssl.get()) == SSL_EARLY_DATA_REJECTED);
    LT_CHECK(early.server.failure().code != hd::quic_tls_code::secret_error);
LT_END_AUTO_TEST(post_handshake_tickets_resume_with_early_data_disabled_and_explicit_offer_rejected)
LT_BEGIN_AUTO_TEST(quic_tls_handshake_suite, initial_client_verification_survives_sni_selection)
    auto config = quic_test::credentials();
    config.hosts.push_back(tls_test::credentials("b").hosts[0]);
    config.hosts[1].alpn = {"h3"};
    config.hosts[1].client_auth.mode = httpserver::server::tls_client_certificate_mode::require;
    config.hosts[1].trust_roots_pem = tls_test::pem("data/tls_credentials/client-root.pem");
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(config).ok());
    for (const char* identity : {static_cast<const char*>(nullptr), "client-valid", "client-expired", "client-other"}) {
        quic_test::connection connection(registry.acquire()->select_default(), mtls_test::authenticated_client(TLS1_3_VERSION, identity), "b.example");
        const bool accepted = identity && std::string(identity) == "client-valid";
        LT_CHECK(connection.connect() == accepted);
        const auto metadata = connection.server.peer_metadata();
        if (accepted) {
            LT_ASSERT(metadata);
            LT_CHECK(metadata->has_client_certificate && metadata->client_certificate_verified);
            LT_CHECK(!metadata->fingerprint_sha256.empty());
        } else {
            LT_CHECK(!metadata);
        }
    }
LT_END_AUTO_TEST(initial_client_verification_survives_sni_selection)
LT_BEGIN_AUTO_TEST(quic_tls_handshake_suite, quic_isolates_acme_and_external_psk_profiles)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(quic_test::credentials()).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    quic_test::connection normal(registry.acquire()->select_default());
    LT_ASSERT(normal.connect());
    LT_CHECK(normal.client.serial() == 101 && normal.client.alpn() == "h3");
    quic_test::connection acme(registry.acquire()->select_default(), quic_test::client_context(), "a.example", {"acme-tls/1"});
    LT_CHECK(!acme.connect());
    auto runtime = std::make_shared<hd::tls_psk_runtime>();
    auto psk = psk_test::credentials(runtime, [](const auto&, const auto&) { return hd::psk_lookup_result{}; });
    LT_ASSERT(registry.replace(psk).ok());
    quic_test::connection external(registry.acquire()->select_default(), quic_test::client_context(), "psk.example", {"h3"});
    LT_CHECK(!external.connect());
    psk.hosts[0].alpn = {"h3"};
    LT_CHECK(!registry.replace(psk).ok());
LT_END_AUTO_TEST(quic_isolates_acme_and_external_psk_profiles)
LT_BEGIN_AUTO_TEST(quic_tls_handshake_suite, protected_tls_handshake_progresses_while_data_budget_is_full)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(quic_test::credentials()).ok());
    auto root = httpserver::server::resource_budget::root({});
    constexpr auto resource = httpserver::server::resource::quic_reassembly_bytes;
    std::unique_ptr<quic_test::connection> connection;
    const auto critical_capacity = 512U * 1024;
    {
        hd::quic_storage_pool pool(16, critical_capacity, root);
        httpserver::server::reservation saturation;
        LT_ASSERT(pool.data().budget.reserve(resource, 16, saturation).ok());
        connection = std::make_unique<quic_test::connection>(registry.acquire()->select_default(), pool.critical());
        LT_ASSERT(connection->connect());
    }
    LT_CHECK(root.in_use(resource) == 16 + critical_capacity);
    LT_CHECK(connection->server.negotiated_protocol() == hd::tls_negotiated_protocol::h3);
    connection.reset();
    LT_CHECK(root.in_use(resource) == 0);
LT_END_AUTO_TEST(protected_tls_handshake_progresses_while_data_budget_is_full)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
