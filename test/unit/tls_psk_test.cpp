/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <future>
#include <thread>
#include <utility>
#include "./tls_psk_fixture.hpp"
#include "./littletest.hpp"
namespace hh = httpserver::http;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
LT_BEGIN_SUITE(psk_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(psk_suite)
LT_BEGIN_AUTO_TEST(psk_suite, immediate_fragmented_handshake_authenticates_with_one_worker_and_no_queue)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        hd::tls_psk_runtime_options options;
        options.handshake_workers = 1;
        options.handshake_queue = 0;
        auto runtime = std::make_shared<hd::tls_psk_runtime>(options);
        auto config = psk_test::credentials(runtime, [](auto, const auto&) {
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("secret"))};
        });
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        auto context = psk_test::client(version);
        std::unique_ptr<SSL, decltype(&SSL_free)> peer(SSL_new(context.get()), SSL_free);
        psk_test::client_key material{"identity", psk_test::bytes("secret")};
        SSL_set_bio(peer.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_connect_state(peer.get());
        SSL_set_app_data(peer.get(), &material);
        SSL_set_tlsext_host_name(peer.get(), "psk.example");
        SSL_do_handshake(peer.get());
        psk_test::immediate_executor ex;
        hd::io_connection_owner owner(ex);
        psk_test::immediate_transport raw(peer.get());
        hd::tls_io_backend tls(raw, ex, 1, registry.acquire()->select_default(), true);
        hd::tls_handshake_operation handshake(owner, 1, std::chrono::steady_clock::now() + 1s);
        handshake.submit(tls);
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (!handshake.state()->applied() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        tls.close();
        runtime->stop();
        LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
        LT_CHECK(handshake.state()->applied());
        LT_CHECK(handshake.state()->stored_result().code == hh::outcome_code::ok);
        LT_CHECK(SSL_is_init_finished(peer.get()));
    }
LT_END_AUTO_TEST(immediate_fragmented_handshake_authenticates_with_one_worker_and_no_queue)
LT_BEGIN_AUTO_TEST(psk_suite, concurrent_versions_and_identities_authenticate_and_exchange)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto runtime = std::make_shared<hd::tls_psk_runtime>();
        std::atomic<unsigned> calls{0}, errors{0};
        const auto owner = std::this_thread::get_id();
        const std::string alpha = version == TLS1_3_VERSION ? std::string("alpha\0binary", 12) : "alpha";
        auto config = psk_test::credentials(runtime, [&](auto identity, const auto& context) {
            ++calls;
            if (std::this_thread::get_id() == owner || context.credential_generation != 1 || context.selected_host != "psk.example" ||
                context.version != (version == TLS1_2_VERSION ? hd::psk_tls_version::tls12 : hd::psk_tls_version::tls13)) ++errors;
            const auto name = std::string(reinterpret_cast<const char*>(identity.data()), identity.size());
            if (name == alpha || name == "beta") return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes(name == alpha ? "alpha-secret" : "beta-secret"))};
            return hd::psk_lookup_result{};
        });
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection a(registry.acquire()->select_default(), version, alpha, psk_test::bytes("alpha-secret"));
        psk_test::connection b(registry.acquire()->select_default(), version, "beta", psk_test::bytes("beta-secret"));
        a.handshake.submit(*a.tls);
        b.handshake.submit(*b.tls);
        const auto done = [&] { a.tick(); b.tick(); return a.handshake.state()->applied() && b.handshake.state()->applied(); };
        LT_CHECK(a.until(done));
        LT_CHECK(a.handshake.state()->stored_result().code == hh::outcome_code::ok);
        LT_CHECK(b.handshake.state()->stored_result().code == hh::outcome_code::ok);
        LT_CHECK(a.exchange());
        LT_CHECK(b.exchange());
        LT_CHECK_EQ(calls.load(), 2u);
        LT_CHECK_EQ(errors.load(), 0u);
        a.close();
        b.close();
        runtime->stop();
        LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
    }
LT_END_AUTO_TEST(concurrent_versions_and_identities_authenticate_and_exchange)
LT_BEGIN_AUTO_TEST(psk_suite, rejects_unknown_wrong_empty_oversized_throwing_and_application_rejection)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (unsigned failure = 0; failure < 6; ++failure) {
            auto runtime = std::make_shared<hd::tls_psk_runtime>();
            auto config = psk_test::credentials(runtime, [failure](auto identity, const auto&) -> hd::psk_lookup_result {
                if (failure == 0 || identity.empty() || failure == 5) return {};
                if (failure == 4) throw std::runtime_error("credential secret");
                const auto value = failure == 2 ? std::string{} : failure == 3 ? std::string(513, 'x') : "server-key";
                return {hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes(value))};
            });
            hd::tls_credentials_registry registry;
            LT_ASSERT(registry.replace(config).ok());
            psk_test::connection peer(registry.acquire()->select_default(), version, "identity", psk_test::bytes("wrong-key"));
            LT_CHECK(!peer.connect());
            LT_CHECK(peer.handshake.state()->applied());
            LT_CHECK(peer.handshake.state()->stored_result().code == hh::outcome_code::protocol_error);
            LT_CHECK(!peer.tls->peer_metadata());
            peer.close();
            runtime->stop();
            LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
        }
    }
LT_END_AUTO_TEST(rejects_unknown_wrong_empty_oversized_throwing_and_application_rejection)
LT_BEGIN_AUTO_TEST(psk_suite, blocking_lookup_leaves_owner_responsive_and_cancel_completes_before_retirement)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto runtime = std::make_shared<hd::tls_psk_runtime>();
        std::atomic<bool> entered{false}, returned{false};
        std::promise<void> release;
        auto released = release.get_future().share();
        auto config = psk_test::credentials(runtime, [&](auto, const auto&) {
            entered = true;
            released.wait();
            returned = true;
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("secret"))};
        });
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection peer(registry.acquire()->select_default(), version, "identity", psk_test::bytes("secret"));
        // Bounded rescue ensures RED cannot leave a fixture hanging in lookup.
        auto rescue = std::async(std::launch::async, [&] { std::this_thread::sleep_for(250ms); release.set_value(); });
        peer.handshake.submit(*peer.tls);
        std::chrono::steady_clock::duration longest{};
        const auto until = std::chrono::steady_clock::now() + 1s;
        while (!entered && std::chrono::steady_clock::now() < until) {
            const auto before = std::chrono::steady_clock::now();
            peer.tick();
            longest = std::max(longest, std::chrono::steady_clock::now() - before);
            std::this_thread::sleep_for(1ms);
        }
        const auto cancelled = peer.tls->request_cancel(*peer.handshake.state());
        peer.ex.run_pending();
        const bool completed_while_held = peer.handshake.state()->applied() && !returned;
        LT_CHECK(entered);
        LT_CHECK(longest < 100ms);
        LT_CHECK(cancelled == hh::outcome_code::ok);
        LT_CHECK(completed_while_held);
        LT_CHECK(peer.handshake.state()->stored_result().code == hh::outcome_code::cancelled);
        LT_CHECK(!peer.tls->peer_metadata());
        peer.close();
        rescue.get();
        runtime->stop();
        LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
        peer.ex.run_pending();
        LT_CHECK_EQ(peer.raw.pending_count(), std::size_t{0});
        LT_CHECK(peer.handshake.state()->stored_result().code == hh::outcome_code::cancelled);
    }
LT_END_AUTO_TEST(blocking_lookup_leaves_owner_responsive_and_cancel_completes_before_retirement)
LT_BEGIN_AUTO_TEST(psk_suite, held_lookup_timeout_keeps_capacity_and_late_success_cannot_publish_or_rearm)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        hd::tls_psk_runtime_options options;
        options.lookup_workers = 1;
        options.lookup_queue = 0;
        auto runtime = std::make_shared<hd::tls_psk_runtime>(options);
        std::atomic<bool> entered{false};
        std::promise<void> release;
        auto released = release.get_future().share();
        auto config = psk_test::credentials(runtime, [&](auto, const auto&) {
            entered = true;
            released.wait();
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("secret"))};
        });
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection peer(registry.acquire()->select_default(), version, "identity", psk_test::bytes("secret"), "psk.example", 400ms);
        peer.handshake.submit(*peer.tls);
        const bool held = peer.until([&] { return entered.load(); });
        const bool terminal = peer.until([&] { return peer.handshake.state()->applied(); });
        psk_test::connection second(registry.acquire()->select_default(), version, "other", psk_test::bytes("secret"));
        const bool accepted = second.connect();
        second.close();
        runtime->stop();
        const bool early_drain = runtime->drain(std::chrono::steady_clock::now() + 5ms);
        release.set_value();
        const bool drained = runtime->drain(std::chrono::steady_clock::now() + 1s);
        peer.ex.run_pending();
        LT_CHECK(held);
        LT_CHECK(terminal);
        LT_CHECK(peer.handshake.state()->stored_result().code == hh::outcome_code::timeout);
        LT_CHECK(!accepted);
        LT_CHECK(second.handshake.state()->stored_result().code == hh::outcome_code::limit_exceeded);
        LT_CHECK(!early_drain);
        LT_CHECK(drained);
        LT_CHECK(!peer.tls->peer_metadata());
        LT_CHECK(!peer.handshake.state()->claim_terminal());
        LT_CHECK_EQ(peer.raw.pending_count(), std::size_t{0});
        LT_CHECK_EQ(peer.ex.pending(), std::size_t{0});
    }
LT_END_AUTO_TEST(held_lookup_timeout_keeps_capacity_and_late_success_cannot_publish_or_rearm)
LT_BEGIN_AUTO_TEST(psk_suite, selected_host_callback_and_held_generation_survive_replacement)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto runtime = std::make_shared<hd::tls_psk_runtime>();
        std::atomic<bool> entered{false};
        std::atomic<unsigned> errors{0}, new_calls{0}, old_calls{0};
        std::promise<void> release;
        auto released = release.get_future().share();
        auto config = psk_test::credentials(runtime, [&](auto, const auto& context) {
            ++old_calls;
            if (context.selected_host != "psk.example" || context.credential_generation != 1) ++errors;
            entered = true;
            released.wait();
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("old-key"))};
        });
        auto alternative = config.hosts[0];
        alternative.host = "other.example";
        alternative.psk->lookup = [&](auto, const auto& context) {
            if (context.selected_host != "other.example" || context.credential_generation != 1) ++errors;
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("other-key"))};
        };
        config.hosts.push_back(alternative);
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection old(registry.acquire()->select_default(), version, "same-identity", psk_test::bytes("old-key"));
        old.handshake.submit(*old.tls);
        const bool held = old.until([&] { return entered.load(); });
        psk_test::connection other(registry.acquire()->select_default(), version, "same-identity", psk_test::bytes("other-key"), "other.example");
        const bool other_authenticated = other.connect();
        config.hosts[0].psk->lookup = [&](auto, const auto& context) {
            ++new_calls;
            if (context.selected_host != "psk.example" || context.credential_generation != 2) ++errors;
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("new-key"))};
        };
        const bool replaced = registry.replace(config).ok();
        release.set_value();
        const bool retired = old.until([&] { return old.handshake.state()->applied() && SSL_is_init_finished(old.peer.get()); });
        psk_test::connection current(registry.acquire()->select_default(), version, "same-identity", psk_test::bytes("new-key"));
        const bool new_authenticated = current.connect();
        LT_CHECK(held);
        LT_CHECK(other_authenticated);
        LT_CHECK(replaced);
        LT_CHECK(retired);
        LT_CHECK(old.handshake.state()->stored_result().code == hh::outcome_code::ok);
        LT_CHECK(old.exchange());
        LT_CHECK(new_authenticated);
        LT_CHECK_EQ(old_calls.load(), 1u);
        LT_CHECK_EQ(new_calls.load(), 1u);
        LT_CHECK_EQ(errors.load(), 0u);
        old.close();
        other.close();
        current.close();
        runtime->stop();
        LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
    }
LT_END_AUTO_TEST(selected_host_callback_and_held_generation_survive_replacement)
LT_BEGIN_AUTO_TEST(psk_suite, mixed_certificate_psk_hosts_apply_selected_profile_in_both_directions)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (bool psk_default : {false, true}) {
            auto runtime = std::make_shared<hd::tls_psk_runtime>();
            std::atomic<unsigned> calls{0};
            auto config = psk_test::credentials(runtime, [&](auto, const auto&) {
                ++calls;
                return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("psk-key"))};
            });
            auto cert = tls_test::credentials().hosts[0];
            cert.alpn = {"http/1.1"};
            config.hosts.push_back(cert);
            config.default_host = psk_default ? 0 : 1;
            hd::tls_credentials_registry registry;
            LT_ASSERT(registry.replace(config).ok());
            psk_test::connection psk(registry.acquire()->select_default(), version, "identity", psk_test::bytes("psk-key"));
            psk_test::connection certificate(registry.acquire()->select_default(), version, "identity", psk_test::bytes("wrong-psk"), "a.example");
            SSL_set_cipher_list(certificate.peer.get(), "DEFAULT:PSK-AES128-GCM-SHA256");
            LT_CHECK(psk.connect());
            LT_CHECK(certificate.connect());
            LT_CHECK(SSL_get0_peer_certificate(certificate.peer.get()) != nullptr);
            LT_CHECK(SSL_get0_peer_certificate(psk.peer.get()) == nullptr);
            LT_CHECK_EQ(calls.load(), 1u);
            LT_CHECK(psk.exchange());
            LT_CHECK(certificate.exchange());
            psk.close();
            certificate.close();
            runtime->stop();
            LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
        }
    }
LT_END_AUTO_TEST(mixed_certificate_psk_hosts_apply_selected_profile_in_both_directions)
LT_BEGIN_AUTO_TEST(psk_suite, external_session_early_data_is_rejected_and_never_delivered)
    auto runtime = std::make_shared<hd::tls_psk_runtime>();
    auto config = psk_test::credentials(runtime, [](auto, const auto&) {
        return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("secret"))};
    });
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(config).ok());
    psk_test::connection peer(registry.acquire()->select_default(), TLS1_3_VERSION, "identity", psk_test::bytes("secret"));
    peer.material.early = true;
    std::size_t sent = 0;
    const std::string early = "unauthenticated early bytes";
    LT_CHECK_EQ(SSL_write_early_data(peer.peer.get(), early.data(), early.size(), &sent), 1);
    LT_CHECK_EQ(sent, early.size());
    peer.handshake.submit(*peer.tls);
    LT_CHECK(peer.until([&] { return peer.handshake.state()->applied() && SSL_is_init_finished(peer.peer.get()); }));
    LT_CHECK(peer.handshake.state()->stored_result().code == hh::outcome_code::ok);
    LT_CHECK_EQ(SSL_get_early_data_status(peer.peer.get()), SSL_EARLY_DATA_REJECTED);
    LT_CHECK(peer.exchange());
    peer.close();
    runtime->stop();
    LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
LT_END_AUTO_TEST(external_session_early_data_is_rejected_and_never_delivered)
LT_BEGIN_AUTO_TEST(psk_suite, handshake_queue_is_finite_and_expired_jobs_never_lookup)
    auto options = hd::tls_psk_runtime_options{};
    options.handshake_workers = 1;
    options.handshake_queue = 1;
    auto runtime = std::make_shared<hd::tls_psk_runtime>(options);
    std::promise<void> started, release;
    auto released = release.get_future().share();
    const auto admitted = runtime->submit_handshake([&] { started.set_value(); released.wait(); });
    const bool held = started.get_future().wait_for(1s) == std::future_status::ready;
    std::atomic<unsigned> calls{0};
    auto config = psk_test::credentials(runtime, [&](auto, const auto&) { ++calls; return hd::psk_lookup_result{}; });
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(config).ok());
    psk_test::connection queued(registry.acquire()->select_default(), TLS1_3_VERSION, "identity", psk_test::bytes("key"), "psk.example", 30ms);
    psk_test::connection saturated(registry.acquire()->select_default(), TLS1_3_VERSION, "identity", psk_test::bytes("key"));
    queued.handshake.submit(*queued.tls);
    queued.ex.run_pending();
    saturated.handshake.submit(*saturated.tls);
    saturated.ex.run_pending();
    std::this_thread::sleep_for(40ms);
    queued.raw.expire_timers(std::chrono::steady_clock::now());
    queued.ex.run_pending();
    release.set_value();
    queued.close();
    saturated.close();
    runtime->stop();
    const bool drained = runtime->drain(std::chrono::steady_clock::now() + 1s);
    LT_CHECK(admitted == hh::outcome_code::ok);
    LT_CHECK(held);
    LT_CHECK(queued.handshake.state()->stored_result().code == hh::outcome_code::timeout);
    LT_CHECK(saturated.handshake.state()->stored_result().code == hh::outcome_code::limit_exceeded);
    LT_CHECK_EQ(calls.load(), 0u);
    LT_CHECK(!queued.handshake.state()->claim_terminal());
    LT_CHECK(drained);
    LT_CHECK_EQ(queued.raw.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(handshake_queue_is_finite_and_expired_jobs_never_lookup)
LT_BEGIN_AUTO_TEST(psk_suite, adapter_and_executor_destruction_before_late_lookup_return_is_safe)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto runtime = std::make_shared<hd::tls_psk_runtime>();
        std::atomic<bool> entered{false};
        std::promise<void> release;
        auto released = release.get_future().share();
        auto config = psk_test::credentials(runtime, [&](auto, const auto&) {
            entered = true;
            released.wait();
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("secret"))};
        });
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        auto peer = std::make_unique<psk_test::connection>(registry.acquire()->select_default(), version, "identity", psk_test::bytes("secret"));
        const auto state = peer->handshake.state();
        peer->handshake.submit(*peer->tls);
        const bool held = peer->until([&] { return entered.load(); });
        peer.reset();
        const auto code = state->stored_result().code;
        runtime->stop();
        const bool incomplete = !runtime->drain(std::chrono::steady_clock::now() + 5ms);
        release.set_value();
        const bool drained = runtime->drain(std::chrono::steady_clock::now() + 1s);
        LT_CHECK(held);
        LT_CHECK(state->applied());
        LT_CHECK(code == hh::outcome_code::connection_closed);
        LT_CHECK(!state->claim_terminal());
        LT_CHECK(incomplete);
        LT_CHECK(drained);
    }
LT_END_AUTO_TEST(adapter_and_executor_destruction_before_late_lookup_return_is_safe)
LT_BEGIN_AUTO_TEST(psk_suite, identity_policy_and_provider_key_ceiling_are_enforced_without_truncation)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto runtime = std::make_shared<hd::tls_psk_runtime>();
        std::atomic<unsigned> calls{0};
        auto config = psk_test::credentials(runtime, [&](auto, const auto& context) {
            ++calls;
            if (context.maximum_key_bytes != 512) return hd::psk_lookup_result{};
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes(std::string(512, 'k')))};
        });
        config.hosts[0].psk->maximum_identity_bytes = 8;
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection bounded(registry.acquire()->select_default(), version, "identity", psk_test::bytes(std::string(512, 'k')));
        LT_CHECK(bounded.connect());
        LT_CHECK(bounded.exchange());
        psk_test::connection too_long(registry.acquire()->select_default(), version, "identity9", psk_test::bytes(std::string(512, 'k')));
        LT_CHECK(!too_long.connect());
        LT_CHECK_EQ(calls.load(), 1u);
        bounded.close();
        too_long.close();
        runtime->stop();
        LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
    }
LT_END_AUTO_TEST(identity_policy_and_provider_key_ceiling_are_enforced_without_truncation)
LT_BEGIN_AUTO_TEST(psk_suite, psk_resumption_cannot_bypass_replaced_generation)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto runtime = std::make_shared<hd::tls_psk_runtime>();
        std::atomic<unsigned> calls{0};
        auto config = psk_test::credentials(runtime, [&](auto, const auto&) {
            ++calls;
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("old-secret"))};
        });
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection first(registry.acquire()->select_default(), version, "identity", psk_test::bytes("old-secret"));
        LT_CHECK(first.connect());
        mtls_test::session_ptr prior(SSL_get1_session(first.peer.get()), SSL_SESSION_free);
        LT_CHECK(prior != nullptr);
        config.hosts[0].psk->lookup = [&](auto, const auto& context) {
            ++calls;
            if (context.credential_generation != 2) return hd::psk_lookup_result{};
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("new-secret"))};
        };
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection resumed(registry.acquire()->select_default(), version, "identity", psk_test::bytes("old-secret"));
        SSL_set_session(resumed.peer.get(), prior.get());
        LT_CHECK(!resumed.connect());
        LT_CHECK(!resumed.tls->peer_metadata());
        LT_CHECK_EQ(calls.load(), 2u);
        first.close();
        resumed.close();
        runtime->stop();
        LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
    }
LT_END_AUTO_TEST(psk_resumption_cannot_bypass_replaced_generation)
LT_BEGIN_AUTO_TEST(psk_suite, psk_alpn_and_cipher_policies_negotiate_only_supported_http1_aead)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        auto runtime = std::make_shared<hd::tls_psk_runtime>();
        auto config = psk_test::credentials(runtime, [](auto, const auto&) {
            return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("secret"))};
        });
        hd::tls_credentials_registry registry;
        LT_ASSERT(registry.replace(config).ok());
        psk_test::connection http1(registry.acquire()->select_default(), version, "identity", psk_test::bytes("secret"));
        const unsigned char http1_offer[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
        SSL_set_alpn_protos(http1.peer.get(), http1_offer, sizeof(http1_offer));
        LT_CHECK(http1.connect());
        LT_CHECK_EQ(http1.alpn(), std::string("http/1.1"));
        psk_test::connection h2(registry.acquire()->select_default(), version, "identity", psk_test::bytes("secret"));
        const unsigned char h2_offer[] = {2, 'h', '2'};
        SSL_set_alpn_protos(h2.peer.get(), h2_offer, sizeof(h2_offer));
        LT_CHECK(!h2.connect());
        psk_test::connection other_cipher(registry.acquire()->select_default(), version, "identity", psk_test::bytes("secret"));
        if (version == TLS1_2_VERSION) SSL_set_cipher_list(other_cipher.peer.get(), "PSK-AES256-GCM-SHA384");
        else SSL_set_ciphersuites(other_cipher.peer.get(), "TLS_AES_256_GCM_SHA384");
        LT_CHECK(!other_cipher.connect());
        LT_CHECK(!other_cipher.tls->peer_metadata());
        http1.close();
        h2.close();
        other_cipher.close();
        runtime->stop();
        LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
    }
LT_END_AUTO_TEST(psk_alpn_and_cipher_policies_negotiate_only_supported_http1_aead)
LT_BEGIN_AUTO_TEST(psk_suite, success_and_cancel_race_orders_preserve_one_terminal_outcome)
    for (int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        for (bool cancel_first : {false, true}) {
            auto runtime = std::make_shared<hd::tls_psk_runtime>();
            std::atomic<bool> entered{false};
            auto config = psk_test::credentials(runtime, [&](auto, const auto&) {
                entered = true;
                return hd::psk_lookup_result{hd::psk_lookup_status::accepted, hd::secure_bytes(psk_test::bytes("secret"))};
            });
            hd::tls_credentials_registry registry;
            LT_ASSERT(registry.replace(config).ok());
            psk_test::connection peer(registry.acquire()->select_default(), version, "identity", psk_test::bytes("secret"));
            if (cancel_first) {
                peer.handshake.submit(*peer.tls);
                const bool looked_up = peer.until([&] { return entered.load(); });
                const auto cancelled = peer.tls->request_cancel(*peer.handshake.state());
                peer.ex.run_pending();
                LT_CHECK(looked_up);
                LT_CHECK(cancelled == hh::outcome_code::ok);
                LT_CHECK(peer.handshake.state()->stored_result().code == hh::outcome_code::cancelled);
                LT_CHECK(!peer.tls->peer_metadata());
            } else {
                LT_CHECK(peer.connect());
                LT_CHECK(peer.tls->request_cancel(*peer.handshake.state()) == hh::outcome_code::invalid_state);
                LT_CHECK(peer.handshake.state()->stored_result().code == hh::outcome_code::ok);
                LT_CHECK(peer.tls->peer_metadata() != nullptr);
            }
            peer.close();
            runtime->stop();
            LT_CHECK(runtime->drain(std::chrono::steady_clock::now() + 1s));
            peer.ex.run_pending();
            LT_CHECK(!peer.handshake.state()->claim_terminal());
            LT_CHECK_EQ(peer.raw.pending_count(), std::size_t{0});
            LT_CHECK(peer.handshake.state()->stored_result().code == (cancel_first ? hh::outcome_code::cancelled : hh::outcome_code::ok));
        }
    }
LT_END_AUTO_TEST(success_and_cancel_race_orders_preserve_one_terminal_outcome)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
