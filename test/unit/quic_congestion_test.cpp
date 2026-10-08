/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <limits>
#include <httpserver/detail/quic_congestion.hpp>
#include "../littletest.hpp"
namespace hd = httpserver::detail;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""ns;
using std::chrono_literals::operator""us;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""h;
LT_BEGIN_SUITE(congestion_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(congestion_suite)
LT_BEGIN_AUTO_TEST(congestion_suite, initial_window_configuration)
    hd::quic_congestion c;
    LT_CHECK(c.snapshot().window == 12000 && c.snapshot().minimum_window == 2400);
    hd::quic_congestion larger(1500);
    LT_CHECK(larger.snapshot().window == 14720);
    bool rejected = false;
    try {
        hd::quic_congestion invalid(1199);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    LT_CHECK(rejected);
LT_END_AUTO_TEST(initial_window_configuration)
LT_BEGIN_AUTO_TEST(congestion_suite, slow_start_loss_epochs_and_avoidance_credit)
    hd::quic_congestion c;
    c.acknowledge(1200, {}, false);
    LT_CHECK(c.snapshot().window == 13200);
    c.lose({}, {}, false);
    LT_CHECK(c.snapshot().window == 6600);
    c.lose({}, hd::quic_congestion::time_point {} + 1ms, false);
    LT_CHECK(c.snapshot().window == 6600);
    c.acknowledge(6600, {}, false);
    LT_CHECK(c.snapshot().window == 6600);
    for (unsigned i = 0; i < 66; ++i) c.acknowledge(100, hd::quic_congestion::time_point {} + 2ms, false);
    LT_CHECK(c.snapshot().window == 7800);
    c.lose(hd::quic_congestion::time_point {} + 2ms, hd::quic_congestion::time_point {} + 3ms, false);
    LT_CHECK(c.snapshot().window == 3900);
LT_END_AUTO_TEST(slow_start_loss_epochs_and_avoidance_credit)
LT_BEGIN_AUTO_TEST(congestion_suite, limitation_persistent_congestion_and_saturation)
    hd::quic_congestion c;
    c.acknowledge(1200, {}, true);
    LT_CHECK(c.snapshot().window == 12000);
    c.lose({}, {}, true);
    LT_CHECK(c.snapshot().window == 2400);
    c.acknowledge(100, hd::quic_congestion::time_point {} + 1ms, false);
    LT_CHECK(c.snapshot().window == 2500);
    c.acknowledge(std::numeric_limits<std::size_t>::max(), hd::quic_congestion::time_point {} + 1ms, false);
    LT_CHECK(c.snapshot().window >= 2400);
    hd::quic_congestion slow;
    slow.acknowledge(std::numeric_limits<std::size_t>::max(), {}, false);
    LT_CHECK(slow.snapshot().window == std::numeric_limits<std::size_t>::max());
LT_END_AUTO_TEST(limitation_persistent_congestion_and_saturation)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
