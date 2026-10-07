/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <array>
#include <string>
#include <vector>
#include <httpserver/exchange.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/detail/http2_flow_control.hpp>
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
LT_BEGIN_SUITE(http2_flow_control_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_flow_control_suite)
LT_BEGIN_AUTO_TEST(http2_flow_control_suite, checked_windows_exhaust_overflow_and_allow_negative_settings)
    hd::http2_window window;
    LT_CHECK(window.debit(65535)); LT_CHECK(!window.debit(1)); LT_CHECK_EQ(window.available, 0);
    LT_CHECK(window.adjust(-10)); LT_CHECK(!window.debit(0)); LT_CHECK_EQ(window.available, -10);
    LT_CHECK(!window.increase(0)); LT_CHECK(window.increase(11)); LT_CHECK_EQ(window.available, 1);
    LT_CHECK(window.increase(0x7ffffffe)); LT_CHECK(!window.increase(1)); LT_CHECK(!window.adjust(1));
    LT_CHECK_EQ(window.available, 0x7fffffff);
LT_END_AUTO_TEST(checked_windows_exhaust_overflow_and_allow_negative_settings)
LT_BEGIN_AUTO_TEST(http2_flow_control_suite, connection_credit_cannot_be_replaced_by_stream_credit)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned finished = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {}); std::string body(40000, 'b');
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish(); ++finished;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    for (auto id : {1u, 3u}) h2test::append(wire, h2test::frame(1, 5, id, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire));
    std::size_t bytes = 0; std::vector<std::uint32_t> order;
    for (unsigned i = 0; i < 16; ++i) {
        executor.run_pending();
        for (const auto& f : h2test::frames(h2test::output(engine, 1))) if (f.type == 0 && !f.payload.empty()) {
            bytes += f.payload.size();
            order.push_back(f.stream);
        }
    }
    LT_CHECK_EQ(bytes, 65535u); LT_ASSERT(order.size() >= 4);
    LT_CHECK_EQ(order[0], 1u); LT_CHECK_EQ(order[1], 3u); LT_CHECK_EQ(order[2], 1u); LT_CHECK_EQ(order[3], 3u);
    for (auto id : {1u, 3u}) LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, id, h2test::increment(40000))));
    LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 0), 0u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 0, h2test::increment(14465))));
    for (unsigned i = 0; i < 16; ++i) {
        executor.run_pending();
        for (const auto& f : h2test::frames(h2test::output(engine, 2))) if (f.type == 0) bytes += f.payload.size();
    }
    LT_CHECK_EQ(bytes, 80000u); LT_CHECK_EQ(finished, 2u); LT_CHECK(!engine.failure());
LT_END_AUTO_TEST(connection_credit_cannot_be_replaced_by_stream_credit)
LT_BEGIN_AUTO_TEST(http2_flow_control_suite, settings_reduction_stalls_existing_stream_and_new_stream_uses_latest)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal next;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {}); std::string body = "abcd";
        co_await x.writer().write(std::as_bytes(std::span(body)));
        co_await next.wait(); co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(4, 0, 0, h2test::setting(4, 0))));
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get()))));
    executor.run_pending(); next.signal(); executor.run_pending();
    LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 0), 0u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 1, h2test::increment(4))));
    LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 0), 0u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 3, h2test::increment(8))));
    std::string received;
    for (const auto& f : h2test::frames(h2test::output(engine))) if (f.type == 0) {
        LT_CHECK_EQ(f.stream, 3u);
        received.append(f.payload.begin(), f.payload.end());
    }
    LT_CHECK_EQ(received, "abcdabcd");
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 1, h2test::increment(4))));
    received.clear();
    for (const auto& f : h2test::frames(h2test::output(engine))) if (f.type == 0) {
        LT_CHECK_EQ(f.stream, 1u);
        received.append(f.payload.begin(), f.payload.end());
    }
    LT_CHECK_EQ(received, "abcd"); LT_CHECK(!engine.failure());
LT_END_AUTO_TEST(settings_reduction_stalls_existing_stream_and_new_stream_uses_latest)
LT_BEGIN_AUTO_TEST(http2_flow_control_suite, window_update_overflow_keeps_correct_error_scope)
    for (auto id : {0u, 1u}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::resume_signal pause;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> { co_await pause.wait(); }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
        h2test::feed(engine, h2test::frame(8, 0, id, h2test::increment(0x7fffffff)));
        auto out = h2test::output(engine);
        if (!id) {
            LT_ASSERT(engine.failure());
            LT_CHECK(engine.failure()->wire_code == hd::http2_error_code::flow_control_error);
        } else {
            LT_CHECK(!engine.failure());
            auto resets = h2test::resets(out);
            LT_ASSERT_EQ(resets.size(), 1u);
            LT_CHECK_EQ(resets[0].stream, 1u);
            LT_CHECK_EQ(resets[0].code, 3u);
        }
    }
LT_END_AUTO_TEST(window_update_overflow_keeps_correct_error_scope)
LT_BEGIN_AUTO_TEST(http2_flow_control_suite, acknowledged_zero_window_is_granted_on_admission_and_partial_consumption)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal read, finish; std::string received;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({4}); co_await read.wait(); std::array<std::byte, 2> into;
        auto part = co_await x.body().read_some(into); received.assign(reinterpret_cast<const char*>(part.data.data()), part.data.size());
        co_await finish.wait();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));
    auto fields = h2test::get(); fields[0].value = "POST";
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)))); executor.run_pending();
    auto grants = h2test::frames(h2test::output(engine)); LT_ASSERT_EQ(grants.size(), 1u);
    LT_CHECK_EQ(grants[0].stream, 1u); LT_CHECK_EQ(h2test::read_u32(grants[0].payload.data()), 4u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 1, {'a', 'b', 'c', 'd'})));
    LT_CHECK_EQ(h2test::output(engine).size(), 0u);
    read.signal(); executor.run_pending(); LT_CHECK_EQ(received, "ab");
    grants = h2test::frames(h2test::output(engine)); LT_ASSERT_EQ(grants.size(), 2u);
    for (const auto& f : grants) {
        LT_CHECK_EQ(f.type, 8u);
        LT_CHECK_EQ(h2test::read_u32(f.payload.data()), 2u);
    }
    LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, {0, 0, 0, 8})));
    grants = h2test::frames(h2test::output(engine)); LT_ASSERT_EQ(grants.size(), 1u);
    LT_CHECK_EQ(grants[0].stream, 0u); LT_CHECK_EQ(h2test::read_u32(grants[0].payload.data()), 2u);
LT_END_AUTO_TEST(acknowledged_zero_window_is_granted_on_admission_and_partial_consumption)
LT_BEGIN_AUTO_TEST(http2_flow_control_suite, repeated_initial_window_settings_check_intermediate_overflow)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> { co_await pause.wait(); }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(4, 0)));
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 1, h2test::increment(0x7fffffff))));
    auto settings = h2test::setting(4, 1); h2test::append(settings, h2test::setting(4, 0));
    LT_CHECK(!h2test::feed(engine, h2test::frame(4, 0, 0, settings)));
    LT_ASSERT(engine.failure()); LT_CHECK(engine.failure()->wire_code == hd::http2_error_code::flow_control_error);
LT_END_AUTO_TEST(repeated_initial_window_settings_check_intermediate_overflow)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
