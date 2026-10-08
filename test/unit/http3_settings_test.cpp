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
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
