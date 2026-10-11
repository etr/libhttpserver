/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <vector>
#include <httpserver/detail/http2_dispatch.hpp>
#include "./littletest.hpp"
LT_BEGIN_SUITE(http2_dispatch_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_dispatch_suite)
LT_BEGIN_AUTO_TEST(http2_dispatch_suite, bounded_turn_reposts_behind_sibling_work)
    httpserver::manual_executor workers;
    auto owner = std::make_shared<httpserver::detail::http2_dispatch>(workers);
    std::vector<int> order;
    owner->post([&] { order.push_back(1); owner->post([&] { order.push_back(3); }); });
    workers.post([&] { order.push_back(2); });
    workers.run_pending();
    LT_CHECK(order == std::vector<int>({1, 2, 3}));
LT_END_AUTO_TEST(bounded_turn_reposts_behind_sibling_work)
LT_BEGIN_AUTO_TEST(http2_dispatch_suite, overflow_keeps_separate_terminal_slot_and_destroys_captures_outside_lock)
    httpserver::manual_executor workers;
    auto owner = std::make_shared<httpserver::detail::http2_dispatch>(workers);
    unsigned normal = 0, terminal = 0;
    owner->on_overflow([&] { ++terminal; owner->post([&] { ++normal; }); });
    for (unsigned i = 0; i < 100; ++i) owner->post([&] { ++normal; });
    workers.run_pending();
    LT_CHECK_EQ(normal, 0u); LT_CHECK_EQ(terminal, 1u);
    owner->post([&] { ++normal; }); workers.run_pending(); LT_CHECK_EQ(normal, 0u);
LT_END_AUTO_TEST(overflow_keeps_separate_terminal_slot_and_destroys_captures_outside_lock)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
