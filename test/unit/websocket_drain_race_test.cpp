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

#include <atomic>
#include <cstdlib>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <httpserver/detail/websocket_driver.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/concurrency/executor.hpp>
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
namespace {
thread_local bool fail_next_allocation = false;
}
void* operator new(std::size_t size) {
    if (std::exchange(fail_next_allocation, false)) throw std::bad_alloc();
    if (auto memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
LT_BEGIN_SUITE(drain_race_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(drain_race_suite)
LT_BEGIN_AUTO_TEST(drain_race_suite, concurrent_terminal_causes_wake_both_operations_once)
    std::atomic<int> callbacks{0}; int receives = 0, writable = 0;
    httpserver::detail::websocket_driver driver(ws_test::small()); auto session = driver.take_session();
    session.on_close([&](auto) { ++callbacks; });
    auto payload = ws_test::bytes("12345678"); session.try_send(httpserver::websocket::message_kind::text, payload);
    session.try_send(httpserver::websocket::message_kind::text, payload);
    httpserver::manual_executor executor;
    httpserver::spawn(executor, session.receive(), [&](auto r) { LT_CHECK(!r.value().status.ok()); ++receives; });
    httpserver::spawn(executor, session.writable(), [&](auto r) { LT_CHECK(!r.value().ok()); ++writable; });
    executor.run_pending(); LT_CHECK_EQ(receives, 0); LT_CHECK_EQ(writable, 0);
    std::thread one([&] { driver.cancel(); }); std::thread two([&] { driver.eof(); });
    one.join(); two.join(); executor.run_pending();
    LT_CHECK_EQ(callbacks.load(), 1); LT_CHECK_EQ(receives, 1); LT_CHECK_EQ(writable, 1);
    LT_CHECK(session.try_send(httpserver::websocket::message_kind::text, payload).disposition == httpserver::websocket::send_disposition::closed);
LT_END_AUTO_TEST(concurrent_terminal_causes_wake_both_operations_once)
LT_BEGIN_AUTO_TEST(drain_race_suite, close_allocation_failure_settles_once_and_wakes_parked_receive)
    int callbacks = 0, receives = 0;
    httpserver::detail::websocket_driver driver; auto session = driver.take_session();
    session.on_close([&](auto info) { ++callbacks; LT_CHECK(info.status.code() == httpserver::http::outcome_code::limit_exceeded); });
    httpserver::manual_executor executor;
    httpserver::spawn(executor, session.receive(), [&](auto r) { ++receives; LT_CHECK(r.value().status.code() == httpserver::http::outcome_code::limit_exceeded); });
    executor.run_pending(); LT_CHECK_EQ(receives, 0);
    fail_next_allocation = true;
    auto result = driver.begin_close(1001, "server drain");
    LT_CHECK(result.code() == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK(driver.snapshot().terminal); LT_CHECK(!driver.snapshot().closing_since);
    executor.run_pending(); LT_CHECK_EQ(receives, 1); LT_CHECK_EQ(callbacks, 1);
LT_END_AUTO_TEST(close_allocation_failure_settles_once_and_wakes_parked_receive)
LT_BEGIN_AUTO_TEST(drain_race_suite, app_peer_drain_and_terminal_race_preserves_one_outcome)
    std::atomic<int> callbacks{0}; httpserver::websocket::close_info observed;
    httpserver::detail::websocket_driver driver; auto session = driver.take_session();
    session.on_close([&](auto info) { observed = std::move(info); ++callbacks; });
    auto peer = ws_test::frame(8, ws_test::bytes(std::string("\x03\xeapeer")));
    std::atomic<bool> go{false};
    std::thread app([&] { while (!go.load()) std::this_thread::yield(); session.close(1000, "app"); });
    std::thread drain([&] { while (!go.load()) std::this_thread::yield(); driver.begin_close(1001, "server drain"); });
    std::thread input([&] { while (!go.load()) std::this_thread::yield(); driver.feed(peer); });
    go = true; driver.cancel({httpserver::http::outcome_code::timeout, "drain deadline"});
    app.join(); drain.join(); input.join();
    LT_CHECK_EQ(callbacks.load(), 1); LT_CHECK(!observed.clean);
    LT_CHECK(observed.status.code() == httpserver::http::outcome_code::timeout);
    LT_CHECK(!observed.code || observed.code == 1000 || observed.code == 1001 || observed.code == 1002);
    const auto reason = observed.reason;
    driver.eof(); driver.cancel(); LT_CHECK_EQ(callbacks.load(), 1); LT_CHECK_EQ(observed.reason, reason);
LT_END_AUTO_TEST(app_peer_drain_and_terminal_race_preserves_one_outcome)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
