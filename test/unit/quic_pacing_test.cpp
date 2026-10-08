/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/quic_pacing.hpp>
#include "../littletest.hpp"
namespace hd = httpserver::detail;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""ns;
using std::chrono_literals::operator""us;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""h;
LT_BEGIN_SUITE(pacing_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(pacing_suite)
LT_BEGIN_AUTO_TEST(pacing_suite, bounded_burst_and_exact_boundary)
    hd::quic_pacing p(1200);
    hd::quic_pacing::time_point zero{};
    LT_CHECK(!p.deadline(zero, 1200, 12000, 100ms));
    p.emitted(zero, 1200, 12000, 100ms);
    p.emitted(zero, 1200, 12000, 100ms);
    auto due = p.deadline(zero, 1200, 12000, 100ms);
    LT_ASSERT(due);
    LT_CHECK(*due == zero + 10ms);
    LT_CHECK(p.deadline(*due - 1ns, 1200, 12000, 100ms).has_value());
    LT_CHECK(!p.deadline(*due, 1200, 12000, 100ms));
    p.emitted(zero + 1h, 2400, 12000, 100ms);
    LT_CHECK(p.deadline(zero + 1h, 1, 12000, 100ms).has_value());
LT_END_AUTO_TEST(bounded_burst_and_exact_boundary)
LT_BEGIN_AUTO_TEST(pacing_suite, wire_bytes_rate_changes_and_clock_extremes)
    hd::quic_pacing p(1200);
    hd::quic_pacing::time_point zero{};
    p.emitted(zero, 2400, 12000, 100ms);
    LT_CHECK(p.deadline(zero, 1200, 2400, 100ms) == zero + 50ms);
    LT_CHECK(p.deadline(zero, 1200, 12000, 0ms) == zero + 100us);
    p.emitted(hd::quic_pacing::time_point::max(), 2400, 12000, 100ms);
    LT_CHECK(p.deadline(hd::quic_pacing::time_point::max(), 1, 12000, 100ms) == hd::quic_pacing::time_point::max());
    LT_CHECK(p.deadline(hd::quic_pacing::time_point::min(), 1200, 12000, 100ms).has_value());
LT_END_AUTO_TEST(wire_bytes_rate_changes_and_clock_extremes)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
