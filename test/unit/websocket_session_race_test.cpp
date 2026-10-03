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
#include <memory>
#include <utility>
#include <thread>
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
using namespace ws_test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(websocket_session_race_test_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(websocket_session_race_test_suite)
LT_BEGIN_AUTO_TEST(websocket_session_race_test_suite, session_feed_cancel_and_registration_races)
    for (int i = 0; i < 250; ++i) {
        httpserver::manual_executor ex;
        std::atomic<int> close_count{0}; int deliveries = 0;
        test_session s(small(), [&](ws::close_info) { ++close_count; });
        httpserver::spawn(ex, s.receive(), [&](httpserver::task_result<ws::receive_result> r) {
            LT_CHECK(r.has_value()); ++deliveries;
        });
        std::atomic<bool> go{false};
        std::thread peer([&] { while (!go.load()) std::this_thread::yield(); s.feed(frame(1, bytes("hi"))); });
        std::thread cancel([&] { while (!go.load()) std::this_thread::yield(); s.cancel({httpserver::http::outcome_code::cancelled, "cancel"}); });
        go = true; ex.run_pending(); peer.join(); cancel.join(); ex.run_pending();
        LT_CHECK_EQ(deliveries, 1); LT_CHECK_EQ(close_count.load(), 1);
        LT_CHECK_EQ(s.usage().incoming_bytes, std::size_t{0});
    }
LT_END_AUTO_TEST(session_feed_cancel_and_registration_races)
LT_BEGIN_AUTO_TEST(websocket_session_race_test_suite, session_duplicate_waiters_reentrancy_and_teardown)
    httpserver::manual_executor ex; int done = 0;
    auto s = std::make_unique<test_session>(small());
    httpserver::spawn(ex, s->receive(), [&](httpserver::task_result<ws::receive_result> r) { LT_CHECK(r.has_value()); ++done; });
    httpserver::spawn(ex, s->receive(), [&](httpserver::task_result<ws::receive_result> r) { LT_CHECK(r.value().status.code() == httpserver::http::outcome_code::invalid_state); ++done; });
    ex.run_pending(); LT_CHECK_EQ(done, 1); s.reset(); ex.run_pending(); LT_CHECK_EQ(done, 2);
    int closed = 0; test_session* ptr = nullptr;
    test_session reentrant(small(), [&](ws::close_info) { ++closed; ptr->eof(); throw 7; });
    ptr = &reentrant; reentrant.eof(); LT_CHECK_EQ(closed, 1);
LT_END_AUTO_TEST(session_duplicate_waiters_reentrancy_and_teardown)
LT_BEGIN_AUTO_TEST(websocket_session_race_test_suite, session_lazy_tasks_retain_core_after_handle_destruction)
    httpserver::manual_executor ex;
    httpserver::task<ws::receive_result> wait;
    { test_session s(small()); wait = s.receive(); }
    int done = 0;
    httpserver::spawn(ex, std::move(wait), [&](httpserver::task_result<ws::receive_result> r) {
        LT_CHECK(r.has_value()); LT_CHECK(!r.value().status.ok()); ++done;
    });
    ex.run_pending(); LT_CHECK_EQ(done, 1);
LT_END_AUTO_TEST(session_lazy_tasks_retain_core_after_handle_destruction)
LT_BEGIN_AUTO_TEST(websocket_session_race_test_suite, session_queued_resume_after_frame_destruction)
    httpserver::manual_executor ex; test_session s(small());
    auto wait = s.receive(); auto frame = wait.release();
    frame.promise().set_affinity(&ex); frame.promise().try_consume();
    // A non-null consumer keeps final_suspend from self-destroying this
    // manually owned frame. Destroy it after a resume has been queued.
    frame.promise().consumer_ = std::noop_coroutine();
    frame.resume(); s.feed(ws_test::frame(1, bytes("x")));
    frame.promise().invalidate(); frame.destroy(); ex.run_pending();
    auto next = receive(s); LT_CHECK(next.status.ok()); LT_CHECK(next.value.has_value());
    auto parked = s.receive(); auto parked_frame = parked.release();
    parked_frame.promise().set_affinity(&ex); parked_frame.promise().try_consume();
    parked_frame.promise().consumer_ = std::noop_coroutine(); parked_frame.resume();
    parked_frame.promise().invalidate(); parked_frame.destroy();
    s.feed(ws_test::frame(1, bytes("y"))); LT_CHECK(receive(s).value->data == bytes("y"));
LT_END_AUTO_TEST(session_queued_resume_after_frame_destruction)
LT_BEGIN_AUTO_TEST(websocket_session_race_test_suite, session_writable_registration_versus_drain_and_cancel)
    for (int i = 0; i < 250; ++i) {
        httpserver::manual_executor ex; test_session s(small());
        s.try_send(ws::message_kind::binary, bytes("12345678")); int done = 0;
        httpserver::spawn(ex, s.writable(), [&](httpserver::task_result<httpserver::http::outcome> r) { LT_CHECK(r.has_value()); ++done; });
        std::thread drain([&] { output(s); });
        ex.run_pending(); drain.join(); ex.run_pending(); LT_CHECK_EQ(done, 1);
        s.cancel({httpserver::http::outcome_code::cancelled, "cancel"}); ex.run_pending(); LT_CHECK_EQ(done, 1);
    }
LT_END_AUTO_TEST(session_writable_registration_versus_drain_and_cancel)
LT_BEGIN_AUTO_TEST(websocket_session_race_test_suite, session_callback_target_destroyed_after_invocation)
    struct probe {
        bool* armed; bool* called; int* premature; test_session** session;
        ~probe() {
            if (*armed && !*called) ++*premature;
            if (*armed && *called) (*session)->usage();
        }
    };
    bool armed = false, called = false; int premature = 0; test_session* ptr = nullptr;
    test_session s(small(), [capture = probe{&armed, &called, &premature, &ptr}, &called](ws::close_info) { called = true; });
    ptr = &s; armed = true; s.eof(); LT_CHECK(called); LT_CHECK_EQ(premature, 0);
LT_END_AUTO_TEST(session_callback_target_destroyed_after_invocation)
LT_BEGIN_AUTO_TEST(websocket_session_race_test_suite, session_close_registration_races_terminal_and_reenters)
    for (int i = 0; i < 100; ++i) {
        test_session s(small());
        std::atomic<int> callbacks{0};
        std::atomic<bool> go{false};
        httpserver::http::outcome registered;
        std::thread registration([&] {
            while (!go.load()) std::this_thread::yield();
            registered = s.on_close([&](ws::close_info info) {
                LT_CHECK(info.status.code() == httpserver::http::outcome_code::connection_closed);
                LT_CHECK(!s.on_close([](ws::close_info) {}).ok());
                s.eof();
                ++callbacks;
            });
        });
        std::thread terminal([&] {
            while (!go.load()) std::this_thread::yield();
            s.eof();
        });
        go = true;
        registration.join(); terminal.join();
        LT_CHECK_EQ(callbacks.load(), registered.ok() ? 1 : 0);
        if (!registered.ok()) LT_CHECK(registered.code() == httpserver::http::outcome_code::invalid_state);
    }
LT_END_AUTO_TEST(session_close_registration_races_terminal_and_reenters)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
