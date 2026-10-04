/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#include <httpserver/detail/websocket_driver.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/concurrency/executor.hpp>
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
LT_BEGIN_SUITE(progress_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(progress_suite)
LT_BEGIN_AUTO_TEST(progress_suite, observer_is_reentrant_and_independent_of_close_callback)
    // Callback storage must outlive both session cancellation and driver teardown.
    int progress = 0, closes = 0;
    httpserver::detail::websocket_driver d(ws_test::small());
    auto session = d.take_session();
    d.observe_progress([&] { ++progress; (void)d.snapshot(); });
    session.on_close([&](auto) { ++closes; });
    auto data = ws_test::bytes("hello");
    session.try_send(httpserver::websocket::message_kind::text, data);
    LT_CHECK_EQ(progress, 1); LT_CHECK(d.snapshot().output_pending);
    auto wire = ws_test::frame(1, data); d.feed(wire);
    int received = 0; httpserver::manual_executor ex;
    httpserver::spawn(ex, session.receive(), [&](auto r) { LT_CHECK(r.value().status.ok()); ++received; });
    ex.run_pending(); LT_CHECK_EQ(received, 1);
    LT_CHECK(progress >= 3); LT_CHECK(d.snapshot().input_ready);
    std::byte buffer[32]; auto count = d.copy_output(buffer); d.consume_output(count);
    LT_CHECK(!d.snapshot().output_pending);
    session.close(); LT_CHECK(d.snapshot().output_pending);
    d.eof(); d.eof(); LT_CHECK(d.snapshot().terminal); LT_CHECK_EQ(closes, 1);
LT_END_AUTO_TEST(observer_is_reentrant_and_independent_of_close_callback)
LT_BEGIN_AUTO_TEST(progress_suite, incoming_capacity_release_retries_exact_suffix)
    int wakes = 0;
    auto limits = ws_test::small(); limits.incoming_messages = 1;
    httpserver::detail::websocket_driver d(limits); auto session = d.take_session();
    d.observe_progress([&] { ++wakes; });
    auto first = ws_test::frame(1, ws_test::bytes("one"));
    auto second = ws_test::frame(1, ws_test::bytes("two"));
    auto combined = first; combined.insert(combined.end(), second.begin(), second.end());
    auto fed = d.feed(combined); LT_CHECK(fed.blocked); LT_CHECK(fed.consumed < combined.size());
    httpserver::manual_executor ex; int received = 0;
    httpserver::spawn(ex, session.receive(), [&](auto r) { LT_CHECK(r.value().value->data == ws_test::bytes("one")); ++received; });
    ex.run_pending(); int before = wakes;
    auto retry = d.feed(std::span(combined).subspan(fed.consumed));
    LT_CHECK_EQ(retry.consumed, combined.size() - fed.consumed);
    httpserver::spawn(ex, session.receive(), [&](auto r) { LT_CHECK(r.value().value->data == ws_test::bytes("two")); ++received; });
    ex.run_pending(); LT_CHECK_EQ(received, 2); LT_CHECK(wakes > before);
LT_END_AUTO_TEST(incoming_capacity_release_retries_exact_suffix)
LT_BEGIN_AUTO_TEST(progress_suite, driver_close_reserves_control_and_preserves_pinned_data)
    int closes = 0;
    httpserver::detail::websocket_driver d(ws_test::small()); auto session = d.take_session();
    session.on_close([&](auto info) { ++closes; LT_CHECK(info.code == 1001); LT_CHECK_EQ(info.reason, "server drain"); });
    auto payload = ws_test::bytes("hello");
    LT_CHECK(session.try_send(httpserver::websocket::message_kind::text, payload).status.ok());
    std::byte buffer[32]; auto count = d.copy_output(buffer);
    LT_CHECK(d.consume_output(1).ok());
    auto before = std::chrono::steady_clock::now();
    LT_CHECK(d.begin_close(1001, "server drain").ok());
    auto anchor = d.snapshot().closing_since;
    LT_CHECK(anchor.has_value()); LT_CHECK(*anchor >= before);
    LT_CHECK(d.begin_close(1000, "replacement").code() == httpserver::http::outcome_code::invalid_state);
    LT_CHECK(session.close().code() == httpserver::http::outcome_code::invalid_state);
    LT_CHECK(d.snapshot().closing_since == anchor);
    LT_CHECK_EQ(d.copy_output(buffer), count - 1); LT_CHECK(d.consume_output(count - 1).ok());
    count = d.copy_output(buffer); LT_CHECK_EQ(count, std::size_t{16});
    LT_CHECK_EQ(std::to_integer<unsigned>(buffer[0]), 136U);
    LT_CHECK(d.consume_output(count).ok()); LT_CHECK(!d.snapshot().output_pending);
    LT_CHECK(d.snapshot().closing_since == anchor); LT_CHECK(!d.snapshot().terminal);
    d.eof(); LT_CHECK_EQ(closes, 1);
LT_END_AUTO_TEST(driver_close_reserves_control_and_preserves_pinned_data)
LT_BEGIN_AUTO_TEST(progress_suite, close_snapshot_precedes_observer_and_terminal_has_no_new_anchor)
    std::optional<std::chrono::steady_clock::time_point> observed;
    httpserver::detail::websocket_driver d(ws_test::small()); auto session = d.take_session();
    d.observe_progress([&] { observed = d.snapshot().closing_since; });
    LT_CHECK(!session.close(1006).ok()); LT_CHECK(!d.snapshot().closing_since);
    LT_CHECK(session.close(1000, "original").ok());
    LT_CHECK(observed == d.snapshot().closing_since); LT_CHECK(observed.has_value());
    auto anchor = observed; d.cancel(); LT_CHECK(d.snapshot().closing_since == anchor);
    httpserver::detail::websocket_driver cancelled; cancelled.cancel();
    LT_CHECK(!cancelled.snapshot().closing_since);
LT_END_AUTO_TEST(close_snapshot_precedes_observer_and_terminal_has_no_new_anchor)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
