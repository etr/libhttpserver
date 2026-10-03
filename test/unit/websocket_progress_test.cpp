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
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
