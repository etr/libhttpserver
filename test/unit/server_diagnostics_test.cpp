/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <httpserver/server/diagnostics.hpp>
#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/resource_charge.hpp>
#include "./littletest.hpp"
namespace srv = httpserver::server;
namespace det = httpserver::detail;
using kind = srv::diagnostic_kind;
LT_BEGIN_SUITE(diagnostics_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(diagnostics_suite)
LT_BEGIN_AUTO_TEST(diagnostics_suite, empty_and_scalar_contract)
    static_assert(std::is_trivially_copyable_v<srv::diagnostic_event>);
    static_assert(sizeof(srv::diagnostic_event) <= 64);
    srv::diagnostics empty;
    LT_CHECK_EQ(empty.counters().dropped_events, 0u);
    LT_CHECK_EQ(empty.dispatch().delivered, 0u);
LT_END_AUTO_TEST(empty_and_scalar_contract)
LT_BEGIN_AUTO_TEST(diagnostics_suite, counters_only_channel_retains_totals_without_queued_delivery)
    srv::diagnostics channel(srv::diagnostics::callback{});
    for (std::size_t i = 0; i < 1000; ++i) det::diagnostics_access::record(channel, {kind::connection_accepted});
    auto root = srv::resource_budget::root({});
    det::diagnostics_access::attach(root, channel);
    srv::reservation seat;
    LT_ASSERT(root.reserve(srv::resource::connections, 1, seat).ok());
    LT_CHECK_EQ(channel.counters().events[static_cast<std::size_t>(kind::connection_accepted)], 1000u);
    LT_CHECK_EQ(channel.counters().active_connections, 1u);
    LT_CHECK_EQ(channel.counters().dropped_events, 0u);
    LT_CHECK_EQ(channel.dispatch().delivered, 0u);
LT_END_AUTO_TEST(counters_only_channel_retains_totals_without_queued_delivery)
LT_BEGIN_AUTO_TEST(diagnostics_suite, bounded_queue_reuses_storage_and_retains_totals)
    std::vector<srv::diagnostic_event> seen;
    srv::diagnostics channel([&](const auto& e) { seen.push_back(e); });
    for (std::size_t i = 0; i < srv::diagnostics::queue_capacity + 7; ++i) det::diagnostics_access::record(channel, {kind::connection_accepted});
    LT_CHECK_EQ(seen.size(), 0u);
    LT_CHECK_EQ(channel.counters().events[static_cast<std::size_t>(kind::connection_accepted)], srv::diagnostics::queue_capacity + 7);
    LT_CHECK_EQ(channel.counters().dropped_events, 7u);
    LT_CHECK_EQ(channel.dispatch(3).delivered, 3u);
    det::diagnostics_access::record(channel, {kind::stream_admitted});
    LT_CHECK_EQ(channel.dispatch().delivered, srv::diagnostics::queue_capacity - 2);
    LT_CHECK_EQ(channel.dispatch().delivered, 0u);
LT_END_AUTO_TEST(bounded_queue_reuses_storage_and_retains_totals)
LT_BEGIN_AUTO_TEST(diagnostics_suite, recursive_dispatch_exceptions_and_entry_bound)
    srv::diagnostics channel;
    httpserver::http::outcome_code recursive{};
    channel = srv::diagnostics([&](const auto&) {
        recursive = channel.dispatch().code;
        det::diagnostics_access::record(channel, {kind::stream_completed});
        throw 1;
    });
    det::diagnostics_access::record(channel, {kind::stream_admitted});
    LT_CHECK_EQ(channel.dispatch().delivered, 1u);
    LT_CHECK(recursive == httpserver::http::outcome_code::invalid_state);
    LT_CHECK_EQ(channel.counters().callback_failures, 1u);
    LT_CHECK_EQ(channel.dispatch(1).delivered, 1u);
LT_END_AUTO_TEST(recursive_dispatch_exceptions_and_entry_bound)
LT_BEGIN_AUTO_TEST(diagnostics_suite, producers_are_bounded_and_totals_survive_drops)
    srv::diagnostics channel([](const auto&) {});
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([&] {
            for (int j = 0; j < 1000; ++j) det::diagnostics_access::record(channel, {kind::stream_cancelled});
        });
    for (auto& t : threads) t.join();
    auto snapshot = channel.counters();
    auto sent = channel.dispatch().delivered;
    LT_CHECK_EQ(snapshot.events[static_cast<std::size_t>(kind::stream_cancelled)], 4000u);
    LT_CHECK_EQ(snapshot.dropped_events + sent, 4000u);
LT_END_AUTO_TEST(producers_are_bounded_and_totals_survive_drops)
LT_BEGIN_AUTO_TEST(diagnostics_suite, budget_rollback_release_and_growth_are_visible_once)
    srv::diagnostics channel([](const auto&) {});
    srv::budget_limits limits;
    limits.set(srv::resource::connections, 2);
    auto root = srv::resource_budget::root(limits);
    det::diagnostics_access::attach(root, channel);
    srv::resource_budget child;
    LT_CHECK_EQ(root.child(limits, child).ok(), true);
    srv::reservation first, second, refused;
    LT_CHECK_EQ(child.reserve(srv::resource::connections, 2, first).ok(), true);
    LT_CHECK_EQ(child.reserve(srv::resource::connections, 1, refused).ok(), false);
    LT_CHECK_EQ(channel.counters().active_connections, 2u);
    LT_CHECK_EQ(channel.counters().events[static_cast<std::size_t>(kind::resource_refused)], 1u);
    second = std::move(first);
    second.release();
    second.release();
    LT_CHECK_EQ(channel.counters().active_connections, 0u);
    det::resource_charge bytes(child, srv::resource::response_queue_bytes);
    LT_CHECK_EQ(bytes.grow(70), true);
    LT_CHECK_EQ(channel.counters().charged_response_queue_bytes, 70u);
    bytes.shrink(20);
    bytes.shrink(bytes.units());
    LT_CHECK_EQ(channel.counters().charged_response_queue_bytes, 0u);
LT_END_AUTO_TEST(budget_rollback_release_and_growth_are_visible_once)
LT_BEGIN_AUTO_TEST(diagnostics_suite, dispatch_keeps_state_alive_when_handle_is_replaced)
    std::size_t calls = 0;
    srv::diagnostics channel([&](const auto&) {
        ++calls;
        channel = {};
    });
    det::diagnostics_access::record(channel, {kind::connection_closed});
    det::diagnostics_access::record(channel, {kind::connection_closed});
    LT_CHECK_EQ(channel.dispatch().delivered, 2u);
    LT_CHECK_EQ(calls, 2u);
LT_END_AUTO_TEST(dispatch_keeps_state_alive_when_handle_is_replaced)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
