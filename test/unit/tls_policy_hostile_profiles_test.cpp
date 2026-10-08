/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#include "./tls_policy_tcp_fixture.hpp"
#include "./littletest.hpp"
namespace tp = tcp_policy;
namespace srv = httpserver::server;
namespace {
struct lookup_record {
    std::mutex mutex;
    std::vector<tp::hd::psk_handshake_context> calls;
    void record(const tp::hd::psk_handshake_context& context) { std::lock_guard lock(mutex); calls.push_back(context); }
};
const char exception_secret[] = "TASK147-lookup-exception-private";
const char pem_secret[] = "TASK147-PEM-private";
const char identity_secret[] = "TASK147-identity-private";
const char key_secret[] = "TASK147-key-private-material-32!";
tp::hd::psk_lookup lookup(lookup_record& record, std::vector<std::byte> key) {
    return [&, key = std::move(key)](auto identity, const auto& context) {
        record.record(context);
        const std::string name(reinterpret_cast<const char*>(identity.data()), identity.size());
        if (name == "throw") throw std::runtime_error(exception_secret);
        if (name != "one" && name != "two") return tp::hd::psk_lookup_result{};
        auto selected = key;
        if (name == "two") selected[0] ^= std::byte{1};
        return tp::hd::psk_lookup_result{tp::hd::psk_lookup_status::accepted, tp::hd::secure_bytes(selected)};
    };
}
bool rejected(tp::connection& peer) {
    return !peer.connect() && peer.handshake.state()->applied() && peer.result() == tp::hh::outcome_code::protocol_error && !peer.tls->peer_metadata();
}
void failed_update(tp::hd::tls_credentials_registry& registry, const tp::hh::outcome& result,
                   const std::shared_ptr<const tp::hd::tls_credentials_snapshot>& pinned) {
    tp::require(!result.ok() && result.message() == "TLS credentials invalid" && registry.acquire() == pinned && ERR_peek_error() == 0);
    for (const auto& secret : {pem_secret, identity_secret, key_secret, exception_secret}) tp::require(result.message().find(secret) == std::string::npos);
}
}  // namespace
LT_BEGIN_SUITE(tls_policy_hostile_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_policy_hostile_suite)
LT_BEGIN_AUTO_TEST(tls_policy_hostile_suite, sni_exact_canonical_and_default_isolation)
    tp::hd::tls_credentials_registry registry;
    auto config = tls_test::credentials();
    auto second = tls_test::credentials("b").hosts[0];
    second.alpn = {"http/1.1"};
    config.hosts.push_back(second);
    LT_ASSERT(registry.replace(config).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const char* name : {"a.example", "b.example", "B.Example.", "unknown.example", "b.example.evil", "other.b.example", static_cast<const char*>(nullptr)}) {
            const bool selected_b = name && (std::string(name) == "b.example" || std::string(name) == "B.Example.");
            tp::connection peer(registry.acquire()->select_default(), mtls_test::client(version), name);
            LT_ASSERT(peer.connect());
            LT_CHECK_EQ(peer.serial(), selected_b ? 202 : 101);
            LT_CHECK_EQ(peer.alpn(), selected_b ? "http/1.1" : "h2");
            LT_CHECK_EQ(SSL_get_verify_result(peer.peer.get()), X509_V_OK);
            LT_CHECK(peer.exchange());
        }
    }
LT_END_AUTO_TEST(sni_exact_canonical_and_default_isolation)
LT_BEGIN_AUTO_TEST(tls_policy_hostile_suite, mtls_modes_reject_untrusted_expired_and_wrong_purpose)
    using mode = srv::tls_client_certificate_mode;
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const auto policy : {mode::none, mode::request, mode::require}) {
            tp::hd::tls_credentials_registry registry;
            LT_ASSERT(registry.replace(mtls_test::credentials(policy)).ok());
            for (const char* identity : {static_cast<const char*>(nullptr), "client-valid", "client-other", "client-expired", "client-wrong-purpose"}) {
                const bool expected = policy == mode::none || (identity ? std::string(identity) == "client-valid" : policy == mode::request);
                tp::connection peer(registry.acquire()->select_default(), mtls_test::authenticated_client(version, identity));
                LT_CHECK_EQ(peer.connect(), expected);
                const auto metadata = peer.tls->peer_metadata();
                if (expected) {
                    LT_ASSERT(metadata != nullptr);
                    LT_CHECK_EQ(metadata->client_certificate_verified, identity && policy != mode::none);
                    LT_CHECK(peer.exchange());
                } else {
                    LT_CHECK(!metadata);
                    LT_CHECK(peer.result() == tp::hh::outcome_code::protocol_error);
                }
            }
        }
    }
LT_END_AUTO_TEST(mtls_modes_reject_untrusted_expired_and_wrong_purpose)
LT_BEGIN_AUTO_TEST(tls_policy_hostile_suite, psk_identity_host_key_and_exception_isolation)
    tp::runtime_owner runtime;
    lookup_record record;
    const auto first_key = psk_test::bytes(key_secret);
    const auto second_key = psk_test::bytes("TASK147-second-private-key-32!!!");
    auto config = psk_test::credentials(runtime.runtime, lookup(record, first_key));
    auto second = psk_test::credentials(runtime.runtime, lookup(record, second_key)).hosts[0];
    second.host = "other.example";
    config.hosts.push_back(second);
    tp::hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(config).ok());
    const auto generation = registry.acquire()->generation();
    tp::runtime_owner drain_before_captures{runtime.runtime};
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const auto& name : {"psk.example", "other.example"}) {
            for (const auto& identity : {"one", "two", identity_secret, "throw"}) {
                auto key = std::string(name) == "psk.example" ? first_key : second_key;
                if (std::string(identity) == "two") key[0] ^= std::byte{1};
                psk_test::client_key material{identity, key};
                tp::connection peer(registry.acquire()->select_default(), psk_test::client(version), name, {"http/1.1"});
                SSL_set_app_data(peer.peer.get(), &material);
                const bool expected = std::string(identity) == "one" || std::string(identity) == "two";
                LT_CHECK_EQ(peer.connect(), expected);
                {
                    std::lock_guard lock(record.mutex);
                    LT_ASSERT(!record.calls.empty());
                    LT_CHECK_EQ(record.calls.back().selected_host, name);
                    LT_CHECK_EQ(record.calls.back().credential_generation, generation);
                    LT_CHECK(record.calls.back().version == (version == TLS1_2_VERSION ? tp::hd::psk_tls_version::tls12 : tp::hd::psk_tls_version::tls13));
                }
                if (expected) {
                    LT_CHECK(peer.exchange());
                } else {
                    LT_CHECK(peer.result() == tp::hh::outcome_code::protocol_error);
                    LT_CHECK(!peer.tls->peer_metadata());
                }
            }
        }
        psk_test::client_key wrong{"one", first_key};
        tp::connection peer(registry.acquire()->select_default(), psk_test::client(version), "other.example");
        SSL_set_app_data(peer.peer.get(), &wrong);
        LT_CHECK(rejected(peer));
    }
    runtime.runtime->stop();
    LT_CHECK(runtime.runtime->drain(tp::clock::now() + std::chrono::seconds(5)));
    LT_CHECK(!record.calls.empty());
    for (const auto& call : record.calls) {
        LT_CHECK_EQ(call.credential_generation, generation);
        LT_CHECK(call.selected_host == "psk.example" || call.selected_host == "other.example");
    }
LT_END_AUTO_TEST(psk_identity_host_key_and_exception_isolation)
LT_BEGIN_AUTO_TEST(tls_policy_hostile_suite, mixed_default_profiles_cannot_bypass_authentication)
    tp::runtime_owner runtime;
    std::atomic<unsigned> calls{0};
    lookup_record record;
    const auto key = psk_test::bytes(key_secret);
    auto psk = psk_test::credentials(runtime.runtime, [&](auto, const auto& context) {
        record.record(context);
        ++calls;
        return tp::hd::psk_lookup_result{tp::hd::psk_lookup_status::accepted, tp::hd::secure_bytes(key)};
    }).hosts[0];
    tp::runtime_owner drain_before_captures{runtime.runtime};
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const auto default_profile : {srv::tls_profile::certificates, srv::tls_profile::external_psk}) {
            auto config = tls_test::credentials();
            config.hosts.push_back(psk);
            auto mtls = mtls_test::credentials(srv::tls_client_certificate_mode::require).hosts[0];
            mtls.host = "required.example";
            config.hosts.push_back(mtls);
            config.default_host = default_profile == srv::tls_profile::certificates ? 0 : 1;
            tp::hd::tls_credentials_registry registry;
            LT_ASSERT(registry.replace(config).ok());
            psk_test::client_key material{"one", key};
            {
                tp::connection peer(registry.acquire()->select_default(), psk_test::client(version), "psk.example");
                SSL_set_app_data(peer.peer.get(), &material);
                LT_ASSERT(peer.connect());
                LT_CHECK(peer.exchange());
            }
            const auto before = calls.load();
            {
                tp::connection certificate(registry.acquire()->select_default(), mtls_test::client(version));
                LT_ASSERT(certificate.connect());
                LT_CHECK_EQ(certificate.serial(), 101);
                LT_CHECK(certificate.exchange());
            }
            {
                tp::connection required(registry.acquire()->select_default(), mtls_test::authenticated_client(version, "client-valid"), "required.example");
                LT_ASSERT(required.connect());
                LT_ASSERT(required.tls->peer_metadata());
                LT_CHECK(required.tls->peer_metadata()->client_certificate_verified);
            }
            {
                tp::connection bypass(registry.acquire()->select_default(), psk_test::client(version), "required.example");
                SSL_set_app_data(bypass.peer.get(), &material);
                LT_CHECK(rejected(bypass));
            }
            LT_ASSERT(registry.publish_acme(acme_test::challenge("psk.example")).ok());
            {
                tp::connection challenge(registry.acquire()->select_default(), acme_test::client(version), "psk.example", {"acme-tls/1"});
                LT_ASSERT(challenge.connect());
                LT_CHECK_EQ(challenge.serial(), 303);
            }
            LT_CHECK_EQ(calls.load(), before);
        }
    }
    runtime.runtime->stop();
    LT_CHECK(runtime.runtime->drain(tp::clock::now() + std::chrono::seconds(5)));
    for (const auto& call : record.calls) {
        LT_CHECK_EQ(call.selected_host, "psk.example");
        LT_CHECK_EQ(call.credential_generation, 1u);
    }
LT_END_AUTO_TEST(mixed_default_profiles_cannot_bypass_authentication)
LT_BEGIN_AUTO_TEST(tls_policy_hostile_suite, acme_exact_sni_sole_alpn_and_trusted_port)
    tp::hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    const auto challenge = acme_test::challenge();
    LT_ASSERT(registry.publish_acme(challenge).ok());
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (const char* name : {"a.example", "A.Example."}) {
            tp::connection peer(registry.acquire()->select_default(), acme_test::client(version), name, {"acme-tls/1"});
            LT_ASSERT(peer.connect());
            LT_CHECK_EQ(peer.serial(), 303);
            LT_CHECK_EQ(peer.alpn(), "acme-tls/1");
            LT_CHECK(peer.digest_matches(challenge));
        }
        for (const char* name : {"unknown.example", "a.example.evil", "other.a.example", static_cast<const char*>(nullptr)}) {
            tp::connection peer(registry.acquire()->select_default(), acme_test::client(version), name, {"acme-tls/1"});
            LT_CHECK(rejected(peer));
            LT_CHECK_EQ(peer.serial(), -1);
        }
        for (const auto& offers : std::vector<std::vector<std::string>>{{"http/1.1"}, {"acme-tls/1", "h2"}, {"h2", "acme-tls/1"}}) {
            tp::connection peer(registry.acquire()->select_default(), mtls_test::client(version), "a.example", offers);
            LT_ASSERT(peer.connect());
            LT_CHECK_EQ(peer.serial(), 101);
            LT_CHECK(!peer.digest_matches(challenge));
            LT_CHECK(peer.exchange());
        }
        for (const auto& transport : {tp::hd::tls_handshake_context{}, tp::hd::tls_handshake_context{tp::hd::tls_transport::tcp, 0}}) {
            tp::connection peer(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"}, true, false, transport);
            LT_CHECK(rejected(peer));
            LT_CHECK_EQ(peer.serial(), -1);
        }
    }
LT_END_AUTO_TEST(acme_exact_sni_sole_alpn_and_trusted_port)
LT_BEGIN_AUTO_TEST(tls_policy_hostile_suite, invalid_writers_preserve_active_peers_and_redact_diagnostics)
    tp::runtime_owner runtime;
    tp::hd::tls_credentials_registry registry;
    auto config = tls_test::credentials();
    LT_ASSERT(registry.replace(config).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    tp::connection active(registry.acquire()->select_default(), mtls_test::client(TLS1_3_VERSION));
    LT_ASSERT(active.connect());
    const auto pinned = registry.acquire();
    const auto generation = pinned->generation();
    bool writer_ok = false;
    std::jthread writer([&] {
        try {
            auto mismatch = config;
            mismatch.hosts[0].private_key_pem = tls_test::pem("data/tls_credentials/b-key.pem");
            failed_update(registry, registry.replace(mismatch), pinned);
            auto malformed = config;
            malformed.hosts[0].certificate_chain_pem = std::string("-----BEGIN CERTIFICATE-----\n") + pem_secret;
            failed_update(registry, registry.replace(malformed), pinned);
            for (unsigned row = 0; row < 4; ++row) {
                auto invalid = psk_test::credentials(runtime.runtime, [](auto, const auto&) { return tp::hd::psk_lookup_result{}; });
                if (row == 0) invalid.hosts[0].certificate_chain_pem = pem_secret;
                if (row == 1) invalid.hosts[0].trust_roots_pem = pem_secret;
                if (row == 2) invalid.hosts[0].client_auth.mode = srv::tls_client_certificate_mode::require;
                if (row == 3) invalid.hosts[0].alpn = {"acme-tls/1"};
                failed_update(registry, registry.replace(invalid), pinned);
            }
            for (unsigned row = 0; row < 3; ++row) {
                auto invalid = acme_test::challenge();
                if (row == 0) invalid.certificate_pem = pem_secret;
                if (row == 1) invalid.key_authorization_sha256.fill(std::byte{0x01});
                if (row == 2) invalid.host = std::string("a.example/") + identity_secret;
                failed_update(registry, registry.publish_acme(invalid), pinned);
            }
            failed_update(registry, registry.remove_acme(std::string("a.example/") + identity_secret), pinned);
            writer_ok = true;
        } catch (...) {}
    });
    LT_CHECK(active.exchange());
    writer.join();
    LT_CHECK(writer_ok);
    LT_CHECK(registry.acquire() == pinned);
    LT_ASSERT(registry.replace(tls_test::credentials("b")).ok());
    LT_CHECK_EQ(registry.acquire()->generation(), generation + 1);
    LT_CHECK(registry.acquire()->select_acme("a.example").has_value());
    tp::connection fresh(registry.acquire()->select_default(), mtls_test::client(TLS1_2_VERSION), "b.example");
    LT_ASSERT(fresh.connect());
    LT_CHECK_EQ(fresh.serial(), 202);
    tp::connection preserved(registry.acquire()->select_default(), acme_test::client(TLS1_3_VERSION), "a.example", {"acme-tls/1"});
    LT_ASSERT(preserved.connect());
    LT_CHECK_EQ(preserved.serial(), 303);
    LT_CHECK(preserved.digest_matches(acme_test::challenge()));
    LT_CHECK(active.exchange());
LT_END_AUTO_TEST(invalid_writers_preserve_active_peers_and_redact_diagnostics)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
