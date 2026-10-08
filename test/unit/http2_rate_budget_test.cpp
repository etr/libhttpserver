/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <string>
#include <vector>
#include <httpserver/exchange.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
LT_BEGIN_SUITE(http2_rate_budget_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_rate_budget_suite)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, drained_control_flood_has_a_persistent_finite_budget)
    for (bool settings : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        unsigned accepted = 0;
        for (; accepted < 300; ++accepted) {
            const auto bytes = settings ? h2test::frame(4) : h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8));
            if (!h2test::feed(engine, bytes)) break;
            h2test::output(engine);
        }
        LT_CHECK(accepted < 300u); LT_ASSERT(engine.failure());
        LT_CHECK(engine.failure()->wire_code == hd::http2_error_code::enhance_your_calm);
        LT_CHECK(engine.failure()->outcome == http::outcome_code::limit_exceeded);
        auto frames = h2test::frames(h2test::output(engine)); LT_ASSERT_EQ(frames.size(), 1u); LT_CHECK_EQ(frames[0].type, 7u);
        LT_CHECK_EQ(h2test::read_u32(frames[0].payload.data() + 4), 11u); LT_CHECK(engine.output().empty());
    }
LT_END_AUTO_TEST(drained_control_flood_has_a_persistent_finite_budget)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, completed_stream_churn_exhausts_before_more_route_dispatch)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok()); unsigned calls = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> { ++calls; x.respond(http::status::from_code(204), {}); co_return; }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
    unsigned accepted = 0;
    for (; accepted < 150; ++accepted) {
        if (!h2test::feed(engine, h2test::frame(1, 5, 1 + 2 * accepted, h2test::encode(encoder, h2test::get())))) break;
        executor.run_pending(); h2test::output(engine); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    }
    LT_CHECK(accepted < 150u); LT_CHECK_EQ(calls, accepted); LT_ASSERT(engine.failure());
    LT_CHECK(engine.failure()->wire_code == hd::http2_error_code::enhance_your_calm);
LT_END_AUTO_TEST(completed_stream_churn_exhausts_before_more_route_dispatch)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, exact_control_boundaries_refill_monotonically_and_fragmented_frames_charge_once)
    using namespace std::chrono_literals;  // NOLINT(build/namespaces)
    for (bool fragmented : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits;
        limits.connection.control_events_per_interval = 3; limits.connection.control_interval = 1s;
        hd::http2_request_engine engine(budget, routes, executor, limits);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));  // second event, retires the ACK deadline
        const auto ping = h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8));
        auto send = [&](hd::http2_connection::time_point now) {
            if (!fragmented) return h2test::feed(engine, ping, now);
            for (auto byte : ping) if (!h2test::feed(engine, std::span(&byte, 1), now)) return false;
            return true;
        };
        const auto epoch = hd::http2_connection::time_point{};
        LT_ASSERT(send(epoch + 999ms)); h2test::output(engine);
        LT_ASSERT(send(epoch + 1s)); h2test::output(engine);
        LT_ASSERT(send(epoch + 1001ms)); h2test::output(engine);
        LT_ASSERT(send(epoch - 1s)); h2test::output(engine);  // no backward refill
        LT_CHECK(!send(epoch + 1999ms)); LT_ASSERT(engine.failure());
        LT_CHECK(engine.failure()->outcome == http::outcome_code::limit_exceeded);
    }
LT_END_AUTO_TEST(exact_control_boundaries_refill_monotonically_and_fragmented_frames_charge_once)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, all_control_kinds_and_recoverable_errors_share_the_budget)
    const std::vector<std::vector<std::uint8_t>> events{
        h2test::frame(6, 1, 0, std::vector<std::uint8_t>(8)), h2test::frame(4),
        h2test::frame(2, 0, 1, {0, 0, 0, 0, 1}), h2test::frame(8, 0, 0, h2test::increment(1)),
        h2test::frame(3, 0, 1, h2test::increment(8)), h2test::frame(99, 0, 0, {1}),
        h2test::frame(2, 0, 1, {0, 0, 0, 1, 1}), h2test::frame(8, 0, 1, h2test::increment(0))};
    for (const auto& event : events) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits; limits.connection.control_events_per_interval = 3;
        hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
        auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); h2test::output(engine);
        LT_ASSERT(h2test::feed(engine, event)); h2test::output(engine);
        LT_CHECK(!h2test::feed(engine, event)); LT_ASSERT(engine.failure());
        LT_CHECK(engine.failure()->wire_code == hd::http2_error_code::enhance_your_calm);
    }
LT_END_AUTO_TEST(all_control_kinds_and_recoverable_errors_share_the_budget)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, reset_and_refused_streams_consume_opening_budget_but_trailers_do_not)
    using namespace std::chrono_literals;  // NOLINT(build/namespaces)
    for (bool refused : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        unsigned calls = 0; httpserver::resume_signal pause;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> { ++calls; co_await pause.wait(); }).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits;
        limits.max_streams = 1; limits.connection.stream_openings_per_interval = 2;
        hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));
        for (unsigned id : {1u, 3u}) {
            LT_ASSERT(h2test::feed(engine, h2test::frame(1, 4, id, h2test::encode(encoder, h2test::get())))); executor.run_pending();
            if (!refused) {
                LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, id, h2test::encode(encoder, {{"x-end", "ok"}}))));
                LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, id, h2test::increment(8)))); }
            h2test::output(engine);
        }
        LT_CHECK_EQ(calls, refused ? 1u : 2u);
        LT_CHECK(!h2test::feed(engine, h2test::frame(1, 5, 5, h2test::encode(encoder, h2test::get()))));
        executor.run_pending(); LT_CHECK_EQ(calls, refused ? 1u : 2u);
        LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u); LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 0u);
        pause.signal(); executor.run_pending(); auto out = h2test::output(engine); LT_CHECK_EQ(h2test::count_type(out, 7), 1u);
    }
LT_END_AUTO_TEST(reset_and_refused_streams_consume_opening_budget_but_trailers_do_not)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, stream_allowance_refills_and_data_transfer_is_not_control_work)
    using namespace std::chrono_literals;  // NOLINT(build/namespaces)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned calls = 0; std::string received;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.admit_body({16}); auto body = co_await x.body().collect(16);
        received.assign(reinterpret_cast<const char*>(body.data.data()), body.data.size()); x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_limits limits;
    limits.connection.control_events_per_interval = 4; limits.connection.stream_openings_per_interval = 1;
    hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));
    auto fields = h2test::get(); fields[0].value = "POST";
    for (unsigned id : {1u, 3u}) {
        const auto now = hd::http2_connection::time_point {} + (id == 1 ? 0s : 1s);
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 4, id, h2test::encode(encoder, fields)), now)); executor.run_pending(); h2test::output(engine);
        for (unsigned i = 0; i < 8; ++i) LT_ASSERT(h2test::feed(engine, h2test::frame(0, i == 7 ? 1 : 0, id, {'d'}), now));
        executor.run_pending(); h2test::output(engine); LT_CHECK_EQ(received, "dddddddd");
    }
    LT_CHECK_EQ(calls, 2u); LT_CHECK(!engine.failure());
LT_END_AUTO_TEST(stream_allowance_refills_and_data_transfer_is_not_control_work)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, invalid_policy_fails_before_dispatch_and_extreme_clocks_do_not_overflow)
    using namespace std::chrono_literals;  // NOLINT(build/namespaces)
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits;
        if (mode == 0) limits.connection.control_events_per_interval = 0;
        if (mode == 1) limits.connection.stream_openings_per_interval = 0;
        if (mode == 2) limits.connection.control_interval = 0s;
        if (mode == 3) limits.connection.stream_interval = -1s;
        hd::http2_request_engine engine(budget, routes, executor, limits); LT_ASSERT(engine.failure());
        LT_CHECK(engine.failure()->outcome == http::outcome_code::invalid_argument); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    }
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor executor; hd::http2_request_limits limits; limits.connection.control_events_per_interval = 2;
    hd::http2_request_engine engine(budget, routes, executor, limits);
    LT_ASSERT(h2test::feed(engine, h2test::preface(), hd::http2_connection::time_point::min())); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1), hd::http2_connection::time_point::min()));
    const auto ping = h2test::frame(6, 1, 0, std::vector<std::uint8_t>(8));
    LT_ASSERT(h2test::feed(engine, ping, hd::http2_connection::time_point::max()));
    LT_ASSERT(h2test::feed(engine, ping, hd::http2_connection::time_point::max()));
    LT_CHECK(!h2test::feed(engine, ping, hd::http2_connection::time_point::max()));
LT_END_AUTO_TEST(invalid_policy_fails_before_dispatch_and_extreme_clocks_do_not_overflow)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, ignored_closed_headers_and_continuations_cannot_bypass_control_work_limit)
    for (bool continuation : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits; limits.connection.control_events_per_interval = 3;
        hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface()));
        auto opening = h2test::get(); opening.push_back({":method", "GET"});
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, opening)))); h2test::output(engine);
        auto headers = h2test::encode(encoder, h2test::get());
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, continuation ? 1 : 5, 1, headers)));
        const auto excess = continuation ? h2test::frame(9, 4, 1) : h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get()));
        LT_CHECK(!h2test::feed(engine, excess)); LT_ASSERT(engine.failure());
        LT_CHECK(engine.failure()->wire_code == hd::http2_error_code::enhance_your_calm);
    }
LT_END_AUTO_TEST(ignored_closed_headers_and_continuations_cannot_bypass_control_work_limit)
LT_BEGIN_AUTO_TEST(http2_rate_budget_suite, exhaustion_invalidates_unstarted_handlers_and_releases_pending_response_work)
    for (bool run : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok()); unsigned calls = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> { ++calls; x.respond(http::status::from_code(204), {}); co_return; }).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits; limits.connection.stream_openings_per_interval = 2;
        {
            hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
            LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
            for (unsigned id : {1u, 3u}) LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, id, h2test::encode(encoder, h2test::get()))));
            if (run) executor.run_pending();
            LT_CHECK(!h2test::feed(engine, h2test::frame(1, 5, 5, h2test::encode(encoder, h2test::get())))); executor.run_pending();
            LT_CHECK_EQ(calls, run ? 2u : 0u);
            for (auto kind : {hs::resource::streams, hs::resource::header_bytes, hs::resource::header_fields}) LT_CHECK_EQ(budget.in_use(kind), 0u);
            auto frames = h2test::frames(h2test::output(engine)); LT_ASSERT_EQ(frames.size(), 1u);
            LT_CHECK_EQ(frames[0].type, 7u); LT_CHECK_EQ(h2test::read_u32(frames[0].payload.data()), 3u);
            LT_CHECK_EQ(budget.in_use(hs::resource::response_queue_bytes), 0u);
        }
        for (auto kind : {hs::resource::streams, hs::resource::body_buffer_bytes, hs::resource::header_bytes,
            hs::resource::header_fields, hs::resource::response_queue_bytes}) LT_CHECK_EQ(budget.in_use(kind), 0u);
    }
LT_END_AUTO_TEST(exhaustion_invalidates_unstarted_handlers_and_releases_pending_response_work)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
