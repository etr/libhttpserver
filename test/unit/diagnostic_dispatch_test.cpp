/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <chrono>
#include <future>
#include <thread>
#include <httpserver/server/diagnostics.hpp>
#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/drain_scope.hpp>
#include "./littletest.hpp"
namespace srv = httpserver::server;
namespace hd = httpserver::detail;
using kind = srv::diagnostic_kind;
LT_BEGIN_SUITE(dispatch_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(dispatch_suite)
LT_BEGIN_AUTO_TEST(dispatch_suite, stalled_callback_does_not_hold_producer_or_drain_locks)
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::thread::id callback_thread;
    srv::diagnostics channel([&](const auto&) {
        callback_thread = std::this_thread::get_id();
        entered.set_value();
        channel.counters();
        gate.wait();
    });
    hd::drain_scope scope(channel);
    scope.enter();
    scope.arm(std::chrono::steady_clock::now() + std::chrono::seconds(3), {}, {});
    auto dispatch = std::async(std::launch::async, [&] { return channel.dispatch(1); });
    const bool called = entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    auto io = std::async(std::launch::async, [&] {
        scope.leave();
        return scope.active();
    });
    const bool progressed = io.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const auto concurrent = channel.dispatch().code;
    release.set_value();
    LT_CHECK(called);
    LT_CHECK(progressed);
    LT_CHECK(concurrent == httpserver::http::outcome_code::invalid_state);
    LT_CHECK(callback_thread != std::this_thread::get_id());
    LT_CHECK_EQ(dispatch.get().delivered, 1u);
    LT_CHECK_EQ(io.get(), 0u);
LT_END_AUTO_TEST(stalled_callback_does_not_hold_producer_or_drain_locks)
LT_BEGIN_AUTO_TEST(dispatch_suite, drain_terminal_diagnostics_are_once_and_do_not_need_waiters)
    srv::diagnostics channel([](const auto&) {});
    {
        hd::drain_scope scope(channel);
        scope.enter();
        scope.arm(std::chrono::steady_clock::now() + std::chrono::seconds(2), {}, {});
        scope.leave();
        scope.leave();
        srv::drain_result result;
        scope.wait(result);
        scope.wait(result);
    }
    auto c = channel.counters();
    LT_CHECK_EQ(c.events[static_cast<std::size_t>(kind::drain_started)], 1u);
    LT_CHECK_EQ(c.events[static_cast<std::size_t>(kind::drain_completed)], 1u);
    {
        hd::drain_scope idle(channel);
        idle.arm(std::chrono::steady_clock::now(), {}, {});
    }
    {
        hd::drain_scope expired(channel);
        expired.enter();
        expired.enter();
        expired.arm(std::chrono::steady_clock::now(), {}, {});
        expired.expire_if_due(std::chrono::steady_clock::now());
        expired.leave();
        expired.leave();
    }
    c = channel.counters();
    LT_CHECK_EQ(c.events[static_cast<std::size_t>(kind::drain_completed)], 2u);
    LT_CHECK_EQ(c.events[static_cast<std::size_t>(kind::drain_expired)], 1u);
LT_END_AUTO_TEST(drain_terminal_diagnostics_are_once_and_do_not_need_waiters)

LT_BEGIN_AUTO_TEST(dispatch_suite, completed_scope_cannot_emit_a_second_terminal_on_late_cleanup)
    srv::diagnostics channel([](const auto&) {});
    hd::drain_scope scope(channel);
    scope.arm(std::chrono::steady_clock::now(), {}, {});
    scope.enter(); scope.expire_if_due(std::chrono::steady_clock::now()); scope.leave();
    LT_CHECK_EQ(channel.counters().events[static_cast<std::size_t>(kind::drain_completed)], 1u);
    LT_CHECK_EQ(channel.counters().events[static_cast<std::size_t>(kind::drain_expired)], 0u);
LT_END_AUTO_TEST(completed_scope_cannot_emit_a_second_terminal_on_late_cleanup)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
