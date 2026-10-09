/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/quic_connection_lifecycle.hpp>
#include "./littletest.hpp"
namespace hd = httpserver::detail;
using namespace std::chrono_literals;  // NOLINT(build/namespaces)
using lifecycle = hd::quic_connection_lifecycle;
using time_point = lifecycle::time_point;
LT_BEGIN_SUITE(lifecycle_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(lifecycle_suite)
LT_BEGIN_AUTO_TEST(lifecycle_suite, negotiated_idle_and_first_actual_eliciting_send)
    lifecycle disabled(0ms, 0ms, {});
    LT_CHECK(!disabled.deadline(10ms));
    for (auto values : {std::pair{100ms, 0ms}, std::pair{0ms, 100ms}, std::pair{100ms, 200ms}}) {
        lifecycle c(values.first, values.second, {});
        LT_CHECK(c.deadline(10ms) == time_point {} + 100ms);
        c.emitted(false, true, time_point {} + 10ms);
        c.emitted(true, false, time_point {} + 20ms);
        LT_CHECK(c.deadline(10ms) == time_point {} + 100ms);
        c.emitted(true, true, time_point {} + 30ms);
        c.emitted(true, true, time_point {} + 60ms);
        LT_CHECK(c.deadline(10ms) == time_point {} + 130ms);
        c.received(time_point {} + 70ms);
        c.emitted(true, true, time_point {} + 80ms);
        LT_CHECK(c.deadline(10ms) == time_point {} + 180ms);
        LT_CHECK(c.deadline(100ms) == time_point {} + 380ms);
        LT_CHECK(!c.expire(time_point {} + 379ms, 100ms));
        LT_CHECK(c.expire(time_point {} + 380ms, 100ms));
        LT_CHECK(c.state() == hd::quic_lifecycle_state::closed);
        LT_CHECK(!c.expire(time_point::max(), 100ms));
    }
LT_END_AUTO_TEST(negotiated_idle_and_first_actual_eliciting_send)
LT_BEGIN_AUTO_TEST(lifecycle_suite, close_arbitration_owned_reason_and_preserved_drain_endpoint)
    lifecycle c(0ms, 0ms, {});
    std::array<std::byte, 500> reason;
    reason.fill(std::byte{7});
    LT_CHECK(c.local_close({true, 42, 0, reason}, {}, 10ms));
    reason.fill(std::byte{9});
    LT_ASSERT(c.close_frame(true).reason.size() == lifecycle::max_reason);
    LT_CHECK(c.close_frame(true).reason.front() == std::byte{7});
    LT_CHECK(c.close_frame(true).error == 42);
    auto early = c.close_frame(false);
    LT_CHECK(!early.application && early.error == 0x0c && early.reason.empty());
    LT_CHECK(!c.local_close({false, 99, 8, {}}, {}, 1s));
    LT_CHECK(c.close_frame(true).error == 42);
    LT_CHECK(c.peer_close({false, 99, 0, {}}, time_point {} + 20ms, 1s));
    LT_CHECK(c.state() == hd::quic_lifecycle_state::draining);
    LT_CHECK(c.deadline(1s) == time_point {} + 30ms);
    LT_CHECK(!c.response_due(time_point {} + 21ms));
    LT_CHECK(c.expire(time_point {} + 30ms, 1s));
LT_END_AUTO_TEST(close_arbitration_owned_reason_and_preserved_drain_endpoint)
LT_BEGIN_AUTO_TEST(lifecycle_suite, bounded_close_responses_and_reset_silence)
    lifecycle c(0ms, 0ms, {});
    LT_CHECK(c.local_close({false, 17, 6, {}}, {}, 10ms));
    LT_CHECK(c.response_due({}));
    c.close_emitted(false, {}, 10ms);
    LT_CHECK(c.response_due({}));
    for (unsigned i = 0; i < lifecycle::max_close_responses; ++i) {
        auto now = time_point {} + i * 10ms;
        c.closing_packet(now);
        LT_CHECK(c.response_due(now));
        c.close_emitted(true, now, 10ms);
        c.closing_packet(now);
        LT_CHECK(!c.response_due(now));
    }
    c.closing_packet(time_point {} + 1s);
    LT_CHECK(!c.response_due(time_point {} + 1s));
    lifecycle reset(0ms, 0ms, {});
    LT_CHECK(reset.stateless_reset({}, 10ms));
    LT_CHECK(!reset.stateless_reset({}, 10ms));
    LT_CHECK(reset.state() == hd::quic_lifecycle_state::draining);
    LT_CHECK(!reset.response_due({}));
LT_END_AUTO_TEST(bounded_close_responses_and_reset_silence)
LT_BEGIN_AUTO_TEST(lifecycle_suite, saturation_and_late_timer_delivery)
    lifecycle c(100ms, 0ms, time_point::max() - 1ms);
    LT_CHECK(c.deadline(10ms) == time_point::max());
    LT_CHECK(c.expire(time_point::max(), 10ms));
    lifecycle close(0ms, 0ms, {});
    LT_CHECK(close.local_close({}, time_point::max() - 1ms, lifecycle::duration::max()));
    LT_CHECK(close.deadline(1ms) == time_point::max());
    LT_CHECK(close.expire(time_point::max(), 1ms));
LT_END_AUTO_TEST(saturation_and_late_timer_delivery)
LT_BEGIN_AUTO_TEST(lifecycle_suite, close_response_wake_does_not_expire_the_retention_endpoint)
    lifecycle c(0ms, 0ms, {});
    LT_ASSERT(c.local_close({}, time_point {} + 10ms, 10ms));
    LT_CHECK(c.response_deadline() == time_point {} + 10ms);
    c.close_emitted(true, time_point {} + 10ms, 10ms);
    LT_CHECK(!c.response_deadline());
    c.closing_packet(time_point {} + 11ms);
    LT_CHECK(c.response_deadline() == time_point {} + 20ms);
    LT_CHECK(!c.expire(time_point {} + 20ms, 10ms));
    LT_CHECK(c.deadline(10ms) == time_point {} + 40ms);
LT_END_AUTO_TEST(close_response_wake_does_not_expire_the_retention_endpoint)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
