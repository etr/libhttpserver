/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <array>
#include <chrono>
#include <thread>
#include "../integ/nonce_expiry_wait.hpp"
#include "./littletest.hpp"

LT_BEGIN_SUITE(nonce_expiry_wait_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(nonce_expiry_wait_suite)

LT_BEGIN_AUTO_TEST(nonce_expiry_wait_suite, clock_rollback_does_not_imply_nonce_expiry)
    const std::array<std::uint64_t, 5> wall{100, 98, 99, 100, 101};
    std::size_t observed = 0;
    const bool expired = httpserver_test::wait_for_nonce_expiry("0000000000000064", [&] { return wall.at(observed++); },
        [] {}, std::chrono::seconds(1));
    LT_CHECK(expired);
    LT_CHECK_EQ(observed, wall.size());
LT_END_AUTO_TEST(clock_rollback_does_not_imply_nonce_expiry)

LT_BEGIN_AUTO_TEST(nonce_expiry_wait_suite, unchanged_clock_is_bounded_by_monotonic_budget)
    const auto started = std::chrono::steady_clock::now();
    const bool expired = httpserver_test::wait_for_nonce_expiry("0000000000000064", [] { return std::uint64_t{100}; },
        [] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }, std::chrono::milliseconds(5));
    LT_CHECK(!expired);
    LT_CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
LT_END_AUTO_TEST(unchanged_clock_is_bounded_by_monotonic_budget)

LT_BEGIN_AUTO_TEST(nonce_expiry_wait_suite, malformed_timestamp_is_rejected)
    for (const char* nonce : {"short", "xxxxxxxxxxxxxxxx"}) {
        LT_CHECK_THROW(httpserver_test::wait_for_nonce_expiry(nonce, [] { return std::uint64_t{200}; }, [] {}, std::chrono::seconds(1)));
    }
LT_END_AUTO_TEST(malformed_timestamp_is_rejected)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
