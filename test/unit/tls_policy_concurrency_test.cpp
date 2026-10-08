/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <exception>
#include <memory>
#include <string>
#include "./tls_policy_tcp_fixture.hpp"
#include "./littletest.hpp"
namespace tp = tcp_policy;
namespace {
// Each socket's SSL and executor are driven only by its worker.
template<class Work, class Publish> bool rotation(Work work, Publish publish) {
    std::array<tp::gate, 2> gates;
    std::array<std::exception_ptr, 2> errors{};
    std::array<std::jthread, 2> workers;
    for (unsigned i = 0; i < workers.size(); ++i) {
        workers[i] = std::jthread([&, i] {
            try {
                work(i ? TLS1_3_VERSION : TLS1_2_VERSION, gates[i]);
            } catch (...) {
                errors[i] = std::current_exception();
                gates[i].abort();
            }
        });
    }
    bool published = false;
    try {
        tp::require(gates[0].wait(1) && gates[1].wait(1));
        publish();
        published = true;
        for (auto& gate : gates) gate.signal(2);
    } catch (...) {
        for (auto& gate : gates) gate.abort();
    }
    for (auto& worker : workers) worker.join();
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
    return published;
}
void certificate_round(tp::hd::tls_credentials_registry& registry, bool external, bool kqueue) {
    auto old = registry.acquire();
    std::weak_ptr<const tp::hd::tls_credentials_snapshot> retired = old;
    const auto generation = old->generation();
    auto replacement = tls_test::credentials("b");
    replacement.hosts[0].alpn = {"http/1.1"};
    const bool ok = rotation([&](int version, tp::gate& gate) {
        {
            tp::connection established(old->select_default(), mtls_test::client(version), "a.example", {"h2", "http/1.1"}, external, kqueue);
            tp::require(established.connect() && established.serial() == 101 && established.alpn() == "h2");
            tp::connection parked(old->select_default(), mtls_test::client(version), "a.example", {"h2", "http/1.1"}, external, kqueue);
            tp::require(parked.park());
            gate.signal(1);
            tp::require(gate.wait(2));
            tp::require(parked.generation == generation && parked.connect() && parked.serial() == 101 && parked.alpn() == "h2");
            tp::require(!retired.expired() && established.exchange() && parked.exchange());
        }
        tp::connection fresh(registry.acquire()->select_default(), mtls_test::client(version), "b.example", {"h2", "http/1.1"}, external, kqueue);
        tp::require(fresh.generation == generation + 1 && fresh.connect() && fresh.serial() == 202 && fresh.alpn() == "http/1.1" && fresh.exchange());
    }, [&] {
        tp::require(registry.replace(replacement).ok());
        old.reset();
        tp::require(!retired.expired());
        auto pinned = registry.acquire();
        auto invalid = replacement;
        invalid.hosts[0].private_key_pem = tls_test::pem("data/tls_credentials/a-key.pem");
        const auto failure = registry.replace(invalid);
        tp::require(!failure.ok() && failure.message() == "TLS credentials invalid" && registry.acquire() == pinned && pinned->generation() == generation + 1);
    });
    tp::require(ok);
    old.reset();
    tp::require(retired.expired());
}
}  // namespace
LT_BEGIN_SUITE(tls_policy_concurrency_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_policy_concurrency_suite)
LT_BEGIN_AUTO_TEST(tls_policy_concurrency_suite, handshake_parks_on_real_socket)
    tp::hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    tp::connection peer(registry.acquire()->select_default(), mtls_test::client(TLS1_3_VERSION));
    LT_CHECK(peer.park());
    LT_CHECK(peer.connect());
    LT_CHECK_EQ(peer.serial(), 101);
LT_END_AUTO_TEST(handshake_parks_on_real_socket)
LT_BEGIN_AUTO_TEST(tls_policy_concurrency_suite, concurrent_certificate_generations_and_retirement)
    tp::hd::tls_credentials_registry registry;
    for (unsigned round = 0; round < 4; ++round) {
        LT_ASSERT(registry.replace(tls_test::credentials()).ok());
        certificate_round(registry, true, false);
    }
LT_END_AUTO_TEST(concurrent_certificate_generations_and_retirement)
LT_BEGIN_AUTO_TEST(tls_policy_concurrency_suite, managed_poll_rotation)
    tp::hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    certificate_round(registry, false, false);
LT_END_AUTO_TEST(managed_poll_rotation)
#if defined(__APPLE__) || defined(__FreeBSD__)
LT_BEGIN_AUTO_TEST(tls_policy_concurrency_suite, managed_kqueue_rotation)
    tp::hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    certificate_round(registry, false, true);
LT_END_AUTO_TEST(managed_kqueue_rotation)
#endif
LT_BEGIN_AUTO_TEST(tls_policy_concurrency_suite, mtls_trust_rotation)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        tp::hd::tls_credentials_registry registry;
        auto config = mtls_test::credentials(httpserver::server::tls_client_certificate_mode::require);
        LT_ASSERT(registry.replace(config).ok());
        tp::connection old(registry.acquire()->select_default(), mtls_test::authenticated_client(version, "client-valid"));
        LT_ASSERT(old.park());
        const auto before = old.generation;
        bool published = false;
        std::jthread publisher([&] {
            config.hosts[0].trust_roots_pem = tls_test::pem("data/tls_credentials/client-other-root.pem");
            published = registry.replace(config).ok();
        });
        publisher.join();
        LT_ASSERT(published);
        LT_ASSERT(old.connect());
        LT_CHECK(old.tls->peer_metadata() != nullptr);
        LT_CHECK(old.exchange());
        tp::connection rejected(registry.acquire()->select_default(), mtls_test::authenticated_client(version, "client-valid"));
        LT_CHECK(!rejected.connect());
        LT_CHECK(rejected.tls->peer_metadata() == nullptr);
        tp::connection fresh(registry.acquire()->select_default(), mtls_test::authenticated_client(version, "client-other"));
        LT_CHECK_EQ(fresh.generation, before + 1);
        LT_ASSERT(fresh.connect());
        LT_CHECK(fresh.tls->peer_metadata() != nullptr);
        LT_CHECK(fresh.exchange());
    }
LT_END_AUTO_TEST(mtls_trust_rotation)
LT_BEGIN_AUTO_TEST(tls_policy_concurrency_suite, psk_lookup_pins_host_and_generation)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        tp::runtime_owner runtime;
        tp::hd::tls_credentials_registry registry;
        tp::gate gate;
        const auto old_key = psk_test::bytes("old-secret-key-material-32-bytes!");
        const auto new_key = psk_test::bytes("new-secret-key-material-32-bytes!");
        std::uint64_t seen_generation = 0;
        std::string seen_host;
        LT_ASSERT(registry.replace(psk_test::credentials(runtime.runtime, [&](auto, const auto& context) {
            seen_generation = context.credential_generation;
            seen_host = context.selected_host;
            gate.signal(1);
            if (!gate.wait(2)) return tp::hd::psk_lookup_result{};
            return tp::hd::psk_lookup_result{tp::hd::psk_lookup_status::accepted, tp::hd::secure_bytes(old_key)};
        })).ok());
        const auto before = registry.acquire()->generation();
        bool old_ok = false;
        std::atomic<bool> new_scope_correct{true};
        tp::runtime_owner drain_before_captures{runtime.runtime};
        std::jthread worker([&] {
            try {
                psk_test::client_key material{"device", old_key};
                tp::connection old(registry.acquire()->select_default(), psk_test::client(version), "psk.example");
                SSL_set_app_data(old.peer.get(), &material);
                old_ok = old.connect() && old.exchange() && old.generation == before;
            } catch (...) { gate.abort(); }
        });
        tp::gate_release release{gate};
        const bool parked = gate.wait(1);
        bool published = false;
        bool fresh_ok = false;
        bool stale_rejected = false;
        if (parked) {
            published = registry.replace(psk_test::credentials(runtime.runtime, [&](auto, const auto& context) {
                if (context.selected_host != "psk.example" || context.credential_generation != before + 1) new_scope_correct = false;
                return tp::hd::psk_lookup_result{tp::hd::psk_lookup_status::accepted, tp::hd::secure_bytes(new_key)};
            })).ok();
            psk_test::client_key material{"device", new_key};
            tp::connection fresh(registry.acquire()->select_default(), psk_test::client(version), "psk.example");
            SSL_set_app_data(fresh.peer.get(), &material);
            fresh_ok = fresh.connect() && fresh.exchange() && fresh.generation == before + 1;
            material.key = old_key;
            tp::connection stale(registry.acquire()->select_default(), psk_test::client(version), "psk.example");
            SSL_set_app_data(stale.peer.get(), &material);
            stale_rejected = !stale.connect() && !stale.tls->peer_metadata();
        }
        gate.signal(2);
        worker.join();
        LT_CHECK(parked && published && fresh_ok && stale_rejected && old_ok && new_scope_correct);
        LT_CHECK_EQ(seen_generation, before);
        LT_CHECK_EQ(seen_host, "psk.example");
        runtime.runtime->stop();
        LT_CHECK(runtime.runtime->drain(tp::clock::now() + std::chrono::seconds(5)));
    }
LT_END_AUTO_TEST(psk_lookup_pins_host_and_generation)
LT_BEGIN_AUTO_TEST(tls_policy_concurrency_suite, acme_replacement_removal_during_ordinary_io)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        tp::hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(tls_test::credentials()).ok());
        const auto challenge = acme_test::challenge();
        LT_ASSERT(registry.publish_acme(challenge).ok());
        auto ordinary = std::make_unique<tp::connection>(registry.acquire()->select_default(), mtls_test::client(version));
        LT_ASSERT(ordinary->connect());
        auto snapshot = registry.acquire();
        std::weak_ptr<const tp::hd::tls_credentials_snapshot> retired = snapshot;
        {
            tp::connection old(snapshot->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
            LT_ASSERT(old.park());
            snapshot.reset();
            bool publication_ok = false;
            const auto replacement = acme_test::challenge("a.example", 404, {}, std::byte{0x43});
            std::jthread publisher([&] { publication_ok = registry.publish_acme(replacement).ok(); });
            publisher.join();
            LT_ASSERT(publication_ok);
            LT_ASSERT(old.connect());
            LT_CHECK_EQ(old.serial(), 303);
            LT_CHECK(old.digest_matches(challenge));
            LT_CHECK(!retired.expired());
            tp::connection fresh(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
            LT_ASSERT(fresh.connect());
            LT_CHECK_EQ(fresh.serial(), 404);
            LT_CHECK(fresh.digest_matches(replacement));
            std::array<std::byte, 8> bytes{};
            tp::hd::read_operation read(old.owner, 1, bytes, tp::clock::now() + std::chrono::seconds(5));
            read.submit(*old.tls);
            LT_CHECK(old.until([] {}, [&] { return read.state()->applied(); }));
            LT_CHECK(read.state()->stored_result().code != tp::hh::outcome_code::ok);
            tp::hd::write_operation write(fresh.owner, 1, bytes, tp::clock::now() + std::chrono::seconds(5));
            write.submit(*fresh.tls);
            LT_CHECK(fresh.until([] {}, [&] { return write.state()->applied(); }));
            LT_CHECK(write.state()->stored_result().code != tp::hh::outcome_code::ok);
            tp::connection removing(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
            LT_ASSERT(removing.park());
            bool removed = false;
            std::jthread remover([&] {
                removed = registry.remove_acme("a.example").ok() && registry.replace(tls_test::credentials("b")).ok();
            });
            LT_CHECK(ordinary->exchange());
            remover.join();
            LT_ASSERT(removed);
            LT_ASSERT(removing.connect());
            LT_CHECK_EQ(removing.serial(), 404);
            LT_CHECK(removing.digest_matches(replacement));
            LT_CHECK(!registry.acquire()->select_acme("a.example"));
        }
        // The established ordinary adapter also owns this snapshot.
        LT_CHECK(!retired.expired());
        ordinary.reset();
        LT_CHECK(retired.expired());
        tp::connection removed(registry.acquire()->select_default(), acme_test::client(version), "a.example", {"acme-tls/1"});
        LT_CHECK(!removed.connect());
        LT_CHECK_EQ(removed.serial(), -1);
    }
LT_END_AUTO_TEST(acme_replacement_removal_during_ordinary_io)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
