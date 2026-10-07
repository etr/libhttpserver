/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <atomic>
#include <barrier>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <httpserver/detail/tls_psk_runtime.hpp>
#include "./tls_credentials_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
namespace {
bool coherent(const std::shared_ptr<const hd::tls_credentials_snapshot>& snap) {
    if (!snap || !snap->select_default().context) return false;
    if (snap->hosts().size() == 1) return snap->generation() == 1;
    if (snap->hosts().size() != 2) return false;
    const auto second = snap->select(1);
    return snap->hosts()[1].host == snap->hosts()[0].host + ".second" && second.context && second.snapshot == snap;
}
}  // namespace
LT_BEGIN_SUITE(tls_credentials_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_credentials_suite)
LT_BEGIN_AUTO_TEST(tls_credentials_suite, publication_owns_metadata_and_explicit_default)
    hd::tls_credentials_registry registry;
    LT_CHECK(!registry.acquire());
    auto config = tls_test::credentials();
    config.hosts.push_back(tls_test::credentials("b").hosts[0]);
    config.hosts[0].host = "A.Example.";
    config.default_host = 1;
    LT_ASSERT(registry.replace(config).ok());
    auto snap = registry.acquire();
    config.hosts.clear();
    LT_CHECK_EQ(snap->generation(), std::uint64_t{1});
    LT_CHECK_EQ(snap->default_host(), std::size_t{1});
    LT_CHECK_EQ(snap->hosts()[0].host, std::string("a.example"));
    LT_CHECK_EQ(snap->hosts()[1].host, std::string("b.example"));
    LT_CHECK_EQ(snap->hosts()[0].alpn_wire[0], static_cast<unsigned char>(2));
    LT_CHECK(snap->select_default().context == snap->select(1).context);
    LT_CHECK_THROW(snap->select(2));
LT_END_AUTO_TEST(publication_owns_metadata_and_explicit_default)
LT_BEGIN_AUTO_TEST(tls_credentials_suite, invalid_candidates_preserve_exact_active_generation)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    const auto original = registry.acquire();
    using config = hd::tls_credentials_config;
    const std::vector<std::function<void(config&)>> invalid = {
        [](auto& c) { c.hosts.clear(); }, [](auto& c) { c.default_host = 3; },
        [](auto& c) { c.hosts[0].host.clear(); }, [](auto& c) { c.hosts[0].host = "bad..example"; },
        [](auto& c) { c.hosts[0].host = "-bad.example"; },
        [](auto& c) { auto h = c.hosts[0]; h.host = "A.EXAMPLE."; c.hosts.push_back(h); },
        [](auto& c) { c.hosts[0].certificate_chain_pem.clear(); },
        [](auto& c) { c.hosts[0].private_key_pem.clear(); },
        [](auto& c) { c.hosts[0].certificate_chain_pem = "bad"; },
        [](auto& c) { c.hosts[0].private_key_pem = "bad"; },
        [](auto& c) { c.hosts[0].certificate_chain_pem = "junk" + c.hosts[0].certificate_chain_pem; },
        [](auto& c) { c.hosts[0].private_key_pem = "junk" + c.hosts[0].private_key_pem; },
        [](auto& c) { c.hosts[0].trust_roots_pem += "-----BEGIN CERTIFICATE-----\nbad\n-----END CERTIFICATE-----"; },
        [](auto& c) { c.hosts[0].host = std::string(64, 'a') + ".example"; },
        [](auto& c) { c.hosts[0].profile = static_cast<httpserver::server::tls_profile>(99); },
        [](auto& c) { c.hosts[0].private_key_pem = tls_test::credentials("b").hosts[0].private_key_pem; },
        [](auto& c) { c.hosts[0].certificate_chain_pem += "-----BEGIN CERTIFICATE-----\nbad\n-----END CERTIFICATE-----\n"; },
        [](auto& c) { c.hosts[0].certificate_chain_pem += "trailing junk"; },
        [](auto& c) { c.hosts[0].private_key_pem += "trailing junk"; },
        [](auto& c) { c.hosts[0].private_key_pem += c.hosts[0].private_key_pem; },
        [](auto& c) { c.hosts[0].private_key_pem = "-----BEGIN ENCRYPTED PRIVATE KEY-----\nbad\n-----END ENCRYPTED PRIVATE KEY-----"; },
        [](auto& c) { c.hosts[0].trust_roots_pem = "bad"; },
        [](auto& c) { c.hosts[0].trust_roots_pem += "trailing junk"; },
        [](auto& c) { c.hosts[0].alpn = {""}; },
        [](auto& c) { c.hosts[0].alpn = {std::string(256, 'x')}; },
        [](auto& c) { c.hosts[0].alpn = {"h2", "h2"}; },
        [](auto& c) { c.hosts[0].alpn.clear(); for (int i = 0; i < 300; ++i) c.hosts[0].alpn.push_back(std::to_string(i) + std::string(250, 'x')); },
        [](auto& c) { c.hosts[0].profile = httpserver::server::tls_profile::none; },
        [](auto& c) { c.hosts[0].profile = httpserver::server::tls_profile::external_psk; },
        [](auto& c) { c.hosts[0].profile = httpserver::server::tls_profile::mutual_tls; c.hosts[0].trust_roots_pem.clear(); },
        [](auto& c) { auto h = tls_test::credentials("b").hosts[0]; h.private_key_pem = "bad"; c.hosts.push_back(h); }
    };
    for (const auto& corrupt : invalid) {
        auto candidate = tls_test::credentials();
        corrupt(candidate);
        auto result = registry.replace(candidate);
        LT_CHECK(!result.ok());
        LT_CHECK_EQ(result.message(), std::string("TLS credentials invalid"));
        LT_CHECK(registry.acquire() == original);
        LT_CHECK(registry.acquire()->select_default().context == original->select_default().context);
    }
    LT_ASSERT(registry.replace(tls_test::credentials("b")).ok());
    LT_CHECK_EQ(registry.acquire()->generation(), std::uint64_t{2});
LT_END_AUTO_TEST(invalid_candidates_preserve_exact_active_generation)
LT_BEGIN_AUTO_TEST(tls_credentials_suite, retained_selection_survives_registry_and_concurrent_publication)
    hd::tls_credentials_selection selected;
    std::weak_ptr<const hd::tls_credentials_snapshot> retired;
    {
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(tls_test::credentials()).ok());
        selected = registry.acquire()->select_default();
        retired = selected.snapshot;
        LT_ASSERT(registry.replace(tls_test::credentials("b")).ok());
        LT_CHECK_EQ(selected.snapshot->generation(), std::uint64_t{1});
        LT_CHECK_EQ(selected.snapshot->hosts()[0].host, std::string("a.example"));
    }
    LT_CHECK(!retired.expired());
    selected = {};
    LT_CHECK(retired.expired());
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    std::barrier start(5);
    std::barrier publication_round(4);
    std::atomic<unsigned> errors{0};
    std::vector<std::thread> threads;
    for (unsigned writer = 0; writer < 2; ++writer) {
        threads.emplace_back([&, writer] {
            start.arrive_and_wait();
            for (unsigned i = 0; i < 20; ++i) {
                auto config = tls_test::credentials(writer == 0 ? "a" : "b");
                config.hosts.push_back(config.hosts[0]);
                config.hosts[1].host = config.hosts[0].host + ".second";
                if (!registry.replace(config).ok()) ++errors;
                config.hosts[1].private_key_pem = "invalid";
                if (registry.replace(config).ok()) ++errors;
                publication_round.arrive_and_wait();
                publication_round.arrive_and_wait();
            }
        });
    }
    for (unsigned reader = 0; reader < 2; ++reader) {
        threads.emplace_back([&] {
            start.arrive_and_wait();
            std::uint64_t previous = 0;
            for (unsigned round = 0; round < 20; ++round) {
                for (unsigned i = 0; i < 200; ++i) {
                    const auto snap = registry.acquire();
                    if (snap->generation() < previous || !coherent(snap)) ++errors;
                    previous = snap->generation();
                    std::this_thread::yield();
                }
                publication_round.arrive_and_wait();
                const auto complete = registry.acquire();
                if (complete->generation() != 3 + 2 * round || complete->hosts().size() != 2) ++errors;
                if (!coherent(complete)) ++errors;
                publication_round.arrive_and_wait();
            }
        });
    }
    start.arrive_and_wait();
    for (auto& thread : threads) thread.join();
    LT_CHECK_EQ(errors.load(), 0u);
    LT_CHECK_EQ(registry.acquire()->generation(), std::uint64_t{41});
LT_END_AUTO_TEST(retained_selection_survives_registry_and_concurrent_publication)
LT_BEGIN_AUTO_TEST(tls_credentials_suite, mutual_policy_and_explicit_roots_remain_owned)
    hd::tls_credentials_registry registry;
    auto config = tls_test::credentials();
    config.hosts[0].profile = httpserver::server::tls_profile::mutual_tls;
    config.hosts[0].trust_roots_pem += tls_test::pem("data/tls_credentials/intermediate.pem");
    LT_ASSERT(registry.replace(config).ok());
    LT_CHECK(registry.acquire()->hosts()[0].profile == httpserver::server::tls_profile::mutual_tls);
    config.hosts[0].profile = httpserver::server::tls_profile::certificates;
    config.hosts[0].trust_roots_pem.clear();
    config.hosts[0].alpn.clear();
    LT_ASSERT(registry.replace(config).ok());
    LT_CHECK(registry.acquire()->hosts()[0].alpn_wire.empty());
    const auto oversized = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
    const auto cert = tls_test::credentials().hosts[0].certificate_chain_pem;
    const auto key = tls_test::credentials().hosts[0].private_key_pem;
    LT_CHECK_THROW(hd::tls_context::server_pem(cert, key, std::string_view(cert.data(), oversized)));
LT_END_AUTO_TEST(mutual_policy_and_explicit_roots_remain_owned)
LT_BEGIN_AUTO_TEST(tls_credentials_suite, client_auth_policy_replacement_is_atomic)
    namespace srv = httpserver::server;
    hd::tls_credentials_registry registry;
    auto config = tls_test::credentials();
    config.hosts[0].profile = srv::tls_profile::mutual_tls;
    LT_ASSERT(registry.replace(config).ok());
    auto original = registry.acquire();
    LT_CHECK(original->hosts()[0].client_certificate_mode == srv::tls_client_certificate_mode::require);
    for (int failure = 0; failure < 5; ++failure) {
        auto bad = config;
        if (failure == 0) bad.hosts[0].client_auth.mode = srv::tls_client_certificate_mode::none;
        if (failure == 1) bad.hosts[0].client_auth.mode = static_cast<srv::tls_client_certificate_mode>(99);
        if (failure == 2) {
            bad.hosts[0].profile = srv::tls_profile::certificates;
            bad.hosts[0].client_auth.mode = srv::tls_client_certificate_mode::request;
            bad.hosts[0].trust_roots_pem.clear();
        }
        if (failure == 3) bad.hosts[0].client_auth.timing = srv::tls_client_auth_timing::post_handshake;
        if (failure == 4) bad.hosts[0].client_auth.timing = static_cast<srv::tls_client_auth_timing>(99);
        const auto result = registry.replace(bad);
        LT_CHECK(result.code() == (failure == 3 ? hh::outcome_code::not_supported : hh::outcome_code::invalid_argument));
        LT_CHECK(registry.acquire() == original);
        LT_CHECK(registry.acquire()->select_default().context == original->select_default().context);
    }
    config.hosts[0].client_auth.mode = srv::tls_client_certificate_mode::request;
    LT_ASSERT(registry.replace(config).ok());
    LT_CHECK_EQ(registry.acquire()->generation(), std::uint64_t{2});
    LT_CHECK(registry.acquire()->hosts()[0].client_certificate_mode == srv::tls_client_certificate_mode::request);
LT_END_AUTO_TEST(client_auth_policy_replacement_is_atomic)
LT_BEGIN_AUTO_TEST(tls_credentials_suite, psk_only_publication_validates_all_conflicts_and_bounds)
    namespace srv = httpserver::server;
    hd::tls_credentials_registry registry;
    hd::tls_credentials_config config;
    hd::tls_host_credentials host;
    host.host = "psk.example";
    host.profile = srv::tls_profile::external_psk;
    host.alpn = {"http/1.1"};
    hd::tls_psk_config psk;
    psk.runtime = std::make_shared<hd::tls_psk_runtime>();
    psk.lookup = [](auto, const auto&) { return hd::psk_lookup_result{}; };
    host.psk = psk;
    config.hosts.push_back(host);
    LT_ASSERT(registry.replace(config).ok());
    const auto original = registry.acquire();
    const std::vector<std::function<void(hd::tls_host_credentials&)>> invalid = {
        [](auto& h) { h.psk.reset(); },
        [](auto& h) { h.psk->lookup = {}; },
        [](auto& h) { h.psk->runtime.reset(); },
        [](auto& h) { h.psk->maximum_identity_bytes = 0; },
        [](auto& h) { h.psk->maximum_identity_bytes = 65536; },
        [](auto& h) { h.psk->maximum_key_bytes = 0; },
        [](auto& h) { h.psk->maximum_key_bytes = 513; },
        [](auto& h) { h.psk->maximum_attempts = 0; },
        [](auto& h) { h.psk->maximum_attempts = 65536; },
        [](auto& h) { h.certificate_chain_pem = "forbidden"; },
        [](auto& h) { h.private_key_pem = "forbidden"; },
        [](auto& h) { h.trust_roots_pem = "forbidden"; },
        [](auto& h) { h.client_auth.mode = srv::tls_client_certificate_mode::request; },
        [](auto& h) { h.client_auth.mode = srv::tls_client_certificate_mode::require; },
        [](auto& h) { h.client_auth.timing = srv::tls_client_auth_timing::post_handshake; },
        [](auto& h) { h.alpn = {"h2"}; },
        [](auto& h) { h.alpn = {"h3"}; },
        [](auto& h) { h.alpn = {"custom"}; },
        [](auto& h) { h.profile = srv::tls_profile::certificates; },
        [](auto& h) { h.profile = srv::tls_profile::mutual_tls; }
    };
    for (const auto& corrupt : invalid) {
        auto candidate = config;
        corrupt(candidate.hosts[0]);
        LT_CHECK(!registry.replace(candidate).ok());
        LT_CHECK(registry.acquire() == original);
    }
    auto certificate = tls_test::credentials();
    certificate.hosts[0].psk = psk;
    LT_CHECK(!registry.replace(certificate).ok());
    config.hosts[0].alpn.clear();
    LT_CHECK(registry.replace(config).ok());
    LT_CHECK_EQ(registry.acquire()->generation(), std::uint64_t{2});
    psk.runtime->stop();
    LT_CHECK(!registry.replace(config).ok());
    LT_CHECK(psk.runtime->drain(std::chrono::steady_clock::now() + std::chrono::seconds(1)));
LT_END_AUTO_TEST(psk_only_publication_validates_all_conflicts_and_bounds)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
