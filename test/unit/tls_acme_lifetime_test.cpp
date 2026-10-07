/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <atomic>
#include <barrier>
#include <memory>
#include <thread>
#include <string>
#include <utility>
#include "./tls_acme_peer.hpp"
#include "./tls_acme_adapter.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
using clock_type = std::chrono::system_clock;
LT_BEGIN_SUITE(tls_acme_lifetime_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_acme_lifetime_suite)
LT_BEGIN_AUTO_TEST(tls_acme_lifetime_suite, removal_and_registry_destruction_preserve_acquired_handshakes)
    auto registry = std::make_unique<hd::tls_credentials_registry>();
    LT_ASSERT(registry->replace(tls_test::credentials()).ok());
    const auto a = acme_test::challenge(), b = acme_test::challenge("a.example", 404, {}, std::byte{0x43});
    LT_ASSERT(registry->publish_acme(a).ok());
    auto pinned = registry->acquire()->select_default();
    std::weak_ptr<const hd::tls_credentials_snapshot> old = pinned.snapshot;
    LT_ASSERT(registry->remove_acme("a.example").ok());
    acme_test::connection missing(registry->acquire()->select_default(), acme_test::client(TLS1_3_VERSION), "a.example", {"acme-tls/1"});
    LT_CHECK(!missing.connect());
    LT_ASSERT(registry->publish_acme(b).ok());
    auto fresh = registry->acquire()->select_default();
    registry.reset();
    {
        acme_test::connection previous(std::move(pinned), acme_test::client(TLS1_2_VERSION), "a.example", {"acme-tls/1"});
        LT_ASSERT(previous.connect());
        LT_CHECK_EQ(previous.serial(), std::int64_t{303});
        LT_CHECK(previous.digest_matches(a));
        acme_test::connection current(std::move(fresh), acme_test::client(TLS1_3_VERSION), "a.example", {"acme-tls/1"});
        LT_ASSERT(current.connect());
        LT_CHECK_EQ(current.serial(), std::int64_t{404});
        LT_CHECK(current.digest_matches(b));
        LT_CHECK(!old.expired());
    }
    LT_CHECK(old.expired());
LT_END_AUTO_TEST(removal_and_registry_destruction_preserve_acquired_handshakes)
LT_BEGIN_AUTO_TEST(tls_acme_lifetime_suite, adapter_selects_challenge_and_retirement_keeps_all_owners)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    {
        acme_test::adapter_connection peer(registry.acquire()->select_default());
        LT_ASSERT(peer.connect());
        LT_CHECK_EQ(peer.serial(), std::int64_t{303});
        LT_CHECK_EQ(peer.alpn(), "acme-tls/1");
        std::array<std::byte, 10> bytes{};
        hd::read_operation read(peer.owner, 1, bytes);
        read.submit(*peer.tls);
        peer.drive();
        LT_ASSERT(read.state()->applied());
        LT_CHECK(read.state()->stored_result().code == httpserver::http::outcome_code::protocol_error);
        LT_CHECK_EQ(read.state()->stored_result().transferred, std::size_t{0});
    }
    auto selection = registry.acquire()->select_default();
    std::weak_ptr<const hd::tls_credentials_snapshot> snapshot = selection.snapshot;
    std::weak_ptr<const hd::tls_context> challenge = selection.snapshot->select_acme("a.example", clock_type::now())->context;
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::fake_io_backend raw;
    auto tls = std::make_unique<hd::tls_io_backend>(raw, ex, 1, std::move(selection), true, hd::tls_handshake_context{hd::tls_transport::tcp, 443});
    hd::tls_handshake_operation handshake(owner, 1);
    handshake.submit(*tls);
    ex.run_pending();
    LT_CHECK(raw.pending_count() > 0);
    LT_ASSERT(registry.remove_acme("a.example").ok());
    tls.reset();
    LT_CHECK(!snapshot.expired());
    LT_CHECK(!challenge.expired());
    ex.run_pending();
    LT_CHECK(handshake.state()->applied());
    LT_CHECK(!handshake.state()->claim_terminal());
    LT_CHECK_EQ(owner.pending(), std::size_t{0});
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
    LT_CHECK(snapshot.expired());
    LT_CHECK(challenge.expired());
LT_END_AUTO_TEST(adapter_selects_challenge_and_retirement_keeps_all_owners)
LT_BEGIN_AUTO_TEST(tls_acme_lifetime_suite, simultaneous_publishers_and_normal_replacement_preserve_serialized_updates)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    const auto a = acme_test::challenge(), b = acme_test::challenge("b.example", 404);
    const auto config = tls_test::credentials();
    std::barrier ready(4), finished(4);
    std::atomic<unsigned> errors{0};
    auto writer = [&](unsigned kind) {
        for (unsigned round = 0; round < 8; ++round) {
            ready.arrive_and_wait();
            const auto result = kind == 0 ? registry.publish_acme(a) : (kind == 1 ? registry.publish_acme(b) : registry.replace(config));
            if (!result.ok()) ++errors;
            finished.arrive_and_wait();
        }
    };
    std::thread one(writer, 0), two(writer, 1), three(writer, 2);
    for (unsigned round = 0; round < 8; ++round) {
        ready.arrive_and_wait(); finished.arrive_and_wait();
        const auto snapshot = registry.acquire();
        LT_CHECK(snapshot->select_acme("a.example", clock_type::now()).has_value());
        LT_CHECK(snapshot->select_acme("b.example", clock_type::now()).has_value());
        LT_CHECK_EQ(snapshot->generation(), std::uint64_t{1 + 3 * (round + 1)});
    }
    one.join(); two.join(); three.join();
    LT_CHECK_EQ(errors.load(), 0U);
    for (unsigned order = 0; order < 2; ++order) {
        if (order == 0) {
            LT_ASSERT(registry.remove_acme("a.example").ok());
            LT_ASSERT(registry.replace(config).ok());
        } else {
            LT_ASSERT(registry.replace(config).ok());
            LT_ASSERT(registry.remove_acme("a.example").ok());
        }
        LT_CHECK(!registry.acquire()->select_acme("a.example", clock_type::now()));
        LT_CHECK(registry.acquire()->select_acme("b.example", clock_type::now()).has_value());
        LT_ASSERT(registry.publish_acme(a).ok());
    }
LT_END_AUTO_TEST(simultaneous_publishers_and_normal_replacement_preserve_serialized_updates)
LT_BEGIN_AUTO_TEST(tls_acme_lifetime_suite, ordinary_established_data_survives_challenge_updates)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    acme_test::adapter_connection peer(registry.acquire()->select_default(), false);
    LT_ASSERT(peer.connect());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    LT_ASSERT(registry.remove_acme("a.example").ok());
    const std::string message = "ordinary application data";
    std::array<std::byte, 100> buffer{};
    hd::read_operation read(peer.owner, 1, buffer);
    read.submit(*peer.tls);
    std::size_t count = 0;
    LT_ASSERT(SSL_write_ex(peer.peer.get(), message.data(), message.size(), &count) == 1);
    peer.drive();
    LT_ASSERT(read.state()->applied());
    LT_CHECK_EQ(read.state()->stored_result().transferred, message.size());
    LT_CHECK(std::equal(std::as_bytes(std::span(message)).begin(), std::as_bytes(std::span(message)).end(), buffer.begin()));
LT_END_AUTO_TEST(ordinary_established_data_survives_challenge_updates)
LT_BEGIN_AUTO_TEST(tls_acme_lifetime_suite, concurrent_readers_pin_coherent_challenge_generation_and_invalid_writers_do_not_publish)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    const auto a = acme_test::challenge(), b = acme_test::challenge("a.example", 404, {}, std::byte{0x43});
    LT_ASSERT(registry.publish_acme(a).ok());
    std::barrier ready(3), published(3), finished(3);
    std::atomic<unsigned> failures{0};
    auto observe = [&](hd::tls_credentials_selection selected, int version) {
        const auto serial = selected.snapshot->generation() % 2 == 0 ? 303 : 404;
        try {
            acme_test::connection peer(std::move(selected), acme_test::client(version), "a.example", {"acme-tls/1"});
            if (!peer.connect() || peer.serial() != serial || peer.alpn() != "acme-tls/1" || !peer.digest_matches(serial == 303 ? a : b)) ++failures;
        } catch (...) { ++failures; }
    };
    auto reader = [&](int version) {
        for (unsigned round = 0; round < 8; ++round) {
            auto pinned = registry.acquire()->select_default();
            ready.arrive_and_wait(); published.arrive_and_wait();
            observe(std::move(pinned), version);
            observe(registry.acquire()->select_default(), version);
            finished.arrive_and_wait();
        }
    };
    std::thread one(reader, TLS1_2_VERSION), two(reader, TLS1_3_VERSION);
    for (unsigned round = 0; round < 8; ++round) {
        ready.arrive_and_wait();
        if (!registry.publish_acme(round % 2 == 0 ? b : a).ok()) ++failures;
        const auto active = registry.acquire();
        auto invalid = a; invalid.private_key_pem = "invalid";
        if (registry.publish_acme(invalid).ok() || registry.acquire() != active) ++failures;
        if (registry.remove_acme("*.example").ok() || registry.acquire() != active) ++failures;
        published.arrive_and_wait(); finished.arrive_and_wait();
    }
    one.join(); two.join();
    LT_CHECK_EQ(failures.load(), 0U);
    LT_CHECK_EQ(registry.acquire()->generation(), std::uint64_t{10});
LT_END_AUTO_TEST(concurrent_readers_pin_coherent_challenge_generation_and_invalid_writers_do_not_publish)
LT_BEGIN_AUTO_TEST(tls_acme_lifetime_suite, concurrent_removal_normal_replacement_and_independent_publication_do_not_restore_removed_entries)
    hd::tls_credentials_registry registry;
    const auto config = tls_test::credentials();
    const auto a = acme_test::challenge(), b = acme_test::challenge("b.example", 404);
    LT_ASSERT(registry.replace(config).ok());
    std::barrier ready(4), finished(4);
    std::atomic<unsigned> failures{0};
    auto writer = [&](unsigned kind) {
        for (unsigned round = 0; round < 4; ++round) {
            ready.arrive_and_wait();
            const auto result = kind == 0 ? registry.publish_acme(a) : (kind == 1 ? registry.remove_acme("b.example") : registry.replace(config));
            if (!result.ok()) ++failures;
            finished.arrive_and_wait();
        }
    };
    std::thread one(writer, 0), two(writer, 1), three(writer, 2);
    for (unsigned round = 0; round < 4; ++round) {
        if (!registry.publish_acme(b).ok()) ++failures;
        ready.arrive_and_wait(); finished.arrive_and_wait();
        const auto current = registry.acquire();
        LT_CHECK(current->select_acme("a.example", clock_type::now()).has_value());
        LT_CHECK(!current->select_acme("b.example", clock_type::now()));
        LT_CHECK_EQ(current->generation(), std::uint64_t{1 + 4 * (round + 1)});
    }
    one.join(); two.join(); three.join();
    LT_CHECK_EQ(failures.load(), 0U);
LT_END_AUTO_TEST(concurrent_removal_normal_replacement_and_independent_publication_do_not_restore_removed_entries)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
