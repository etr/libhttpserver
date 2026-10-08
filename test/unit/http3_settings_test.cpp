/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "./http3_fixture.hpp"
#include "./littletest.hpp"
using namespace h3test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(http3_settings_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(http3_settings_suite)
LT_BEGIN_AUTO_TEST(http3_settings_suite, staged_settings_publish_only_when_complete)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
    auto r = feed(c, 2, bytes({0, 4, 6, 1, 0, 7, 0, 6})); LT_CHECK(!r.error);
    LT_CHECK(!c.peer_settings().received);
    r = feed(c, 2, bytes({33})); LT_CHECK(!r.error);
    LT_CHECK(c.peer_settings().received); LT_CHECK_EQ(c.peer_settings().max_field_section, 33U);
LT_END_AUTO_TEST(staged_settings_publish_only_when_complete)
LT_BEGIN_AUTO_TEST(http3_settings_suite, duplicate_forbidden_and_truncated_settings)
    for (auto payload : {bytes({33, 1, 33, 2}), bytes({2, 0}), bytes({3, 0}), bytes({4, 0}), bytes({5, 0}), bytes({1})}) {
        hd::quic_storage_pool pool(200000, 30000, budget());
        hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
        auto wire = bytes({0}); auto f = frame(4, payload); wire.insert(wire.end(), f.begin(), f.end());
        auto r = feed(c, 2, wire); LT_ASSERT(r.error);
        LT_CHECK_EQ(r.error->wire_code, payload.size() == 1 ? 0x106U : 0x109U);
        LT_CHECK(!c.peer_settings().received);
    }
LT_END_AUTO_TEST(duplicate_forbidden_and_truncated_settings)
LT_BEGIN_AUTO_TEST(http3_settings_suite, full_width_values_unknown_settings_and_local_bootstrap)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_connection c(pool.data(), pool.critical()); c.attach_stream(2);
    auto payload = bytes({1}); integer(payload, (1ULL << 62) - 1); integer(payload, 33); integer(payload, 7);
    auto wire = bytes({0}); auto f = frame(4, payload); wire.insert(wire.end(), f.begin(), f.end());
    LT_CHECK(!feed(c, 2, wire).error); LT_CHECK_EQ(c.peer_settings().qpack_capacity, (1ULL << 62) - 1);
    auto prefix = c.local_prefix(hd::http3_role::control);
    LT_CHECK(std::vector<std::byte>(prefix.begin(), prefix.end()) == bytes({0, 4, 4, 1, 0, 7, 0}));
    LT_CHECK(c.advance_local_prefix(hd::http3_role::control, 2));
    LT_CHECK_EQ(c.local_prefix(hd::http3_role::control).size(), 5U);
    LT_CHECK(!c.advance_local_prefix(hd::http3_role::control, 6));
    LT_CHECK(c.advance_local_prefix(hd::http3_role::control, 5));
    LT_CHECK(c.local_prefix(hd::http3_role::control).empty());
    LT_CHECK_EQ(std::to_integer<unsigned>(c.local_prefix(hd::http3_role::qpack_encoder)[0]), 2U);
LT_END_AUTO_TEST(full_width_values_unknown_settings_and_local_bootstrap)
LT_BEGIN_AUTO_TEST(http3_settings_suite, exact_settings_byte_limit_publishes_complete_values)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_limits limits; limits.settings_bytes = 4;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(2);

    auto r = feed(c, 2, bytes({0, 4, 4, 1, 9, 7, 8}));
    LT_CHECK(!r.error); LT_CHECK_EQ(r.consumed, 7U);
    LT_CHECK(c.peer_settings().received); LT_CHECK_EQ(c.peer_settings().qpack_capacity, 9U);
    LT_CHECK_EQ(c.peer_settings().qpack_blocked, 8U);
LT_END_AUTO_TEST(exact_settings_byte_limit_publishes_complete_values)
LT_BEGIN_AUTO_TEST(http3_settings_suite, one_over_settings_byte_limit_rejects_before_payload_reservation)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_limits limits; limits.settings_bytes = 3;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(2);
    const auto before = pool.critical().budget.in_use(hs::resource::quic_reassembly_bytes);

    auto r = feed(c, 2, bytes({0, 4, 4, 1, 9, 7, 8}));
    LT_ASSERT(r.error); LT_CHECK_EQ(r.error->wire_code, 0x107U);
    LT_CHECK(r.error->outcome == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK_EQ(r.consumed, 3U); LT_CHECK(!c.peer_settings().received);
    LT_CHECK_EQ(c.peer_settings().qpack_capacity, 0U); LT_CHECK_EQ(c.peer_settings().qpack_blocked, 0U);
    LT_CHECK_EQ(pool.critical().budget.in_use(hs::resource::quic_reassembly_bytes), before);
LT_END_AUTO_TEST(one_over_settings_byte_limit_rejects_before_payload_reservation)
LT_BEGIN_AUTO_TEST(http3_settings_suite, exact_settings_identifier_limit_counts_ignored_identifier)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_limits limits; limits.settings_identifiers = 2;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(2);

    auto r = feed(c, 2, bytes({0, 4, 4, 1, 9, 33, 8}));
    LT_CHECK(!r.error); LT_CHECK_EQ(r.consumed, 7U);
    LT_CHECK(c.peer_settings().received); LT_CHECK_EQ(c.peer_settings().qpack_capacity, 9U);
LT_END_AUTO_TEST(exact_settings_identifier_limit_counts_ignored_identifier)
LT_BEGIN_AUTO_TEST(http3_settings_suite, ignored_identifier_one_over_limit_prevents_partial_publication)
    hd::quic_storage_pool pool(200000, 30000, budget());
    hd::http3_limits limits; limits.settings_identifiers = 1;
    hd::http3_connection c(pool.data(), pool.critical(), limits); c.attach_stream(2);
    const auto before = pool.critical().budget.in_use(hs::resource::quic_reassembly_bytes);

    auto r = feed(c, 2, bytes({0, 4, 4, 1, 9, 33, 8}));
    LT_ASSERT(r.error); LT_CHECK_EQ(r.error->wire_code, 0x107U);
    LT_CHECK(r.error->outcome == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK_EQ(r.consumed, 7U); LT_CHECK(!c.peer_settings().received);
    LT_CHECK_EQ(c.peer_settings().qpack_capacity, 0U); LT_CHECK_EQ(c.peer_settings().max_field_section, hd::k_quic_max_integer);
    LT_CHECK_EQ(pool.critical().budget.in_use(hs::resource::quic_reassembly_bytes), before);
LT_END_AUTO_TEST(ignored_identifier_one_over_limit_prevents_partial_publication)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
