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
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
LT_BEGIN_SUITE(http2_reset_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_reset_suite)
LT_BEGIN_AUTO_TEST(http2_reset_suite, reset_of_idle_client_or_unsupported_server_stream_is_connection_error)
    for (unsigned id : {1u, 2u, 99u}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_CHECK(!h2test::feed(engine, h2test::frame(3, 0, id, h2test::increment(8))));
        LT_ASSERT(engine.failure()); LT_CHECK_EQ(static_cast<unsigned>(engine.failure()->wire_code), 1u);
        auto frames = h2test::frames(h2test::output(engine)); LT_ASSERT_EQ(frames.size(), 1u); LT_CHECK_EQ(frames[0].type, 7u);
    }
LT_END_AUTO_TEST(reset_of_idle_client_or_unsupported_server_stream_is_connection_error)
LT_BEGIN_AUTO_TEST(http2_reset_suite, reset_discards_unexposed_grants_preserving_borrowed_head_and_sibling)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [](exchange& x) -> task<void> {
        x.admit_body({2}); std::array<std::byte, 2> bytes; co_await x.body().read_some(bytes);
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface()));
    auto initial = engine.output(); LT_ASSERT_EQ(initial[3], 4u); LT_ASSERT(engine.advance_output(initial.size()));
    LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));
    auto fields = h2test::get(); fields[0].value = "POST";
    for (unsigned id : {1u, 3u}) LT_ASSERT(h2test::feed(engine, h2test::frame(1, 4, id, h2test::encode(encoder, fields))));
    executor.run_pending();
    auto head = engine.output(); LT_ASSERT_EQ(head[3], 4u); std::vector<std::uint8_t> saved(head.begin(), head.end());
    LT_ASSERT(engine.advance_output(1));
    LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, h2test::increment(8))));
    auto tail = engine.output(); LT_CHECK(std::equal(tail.begin(), tail.end(), saved.begin() + 1)); LT_ASSERT(engine.advance_output(tail.size()));
    auto frames = h2test::frames(h2test::output(engine)); unsigned grants = 0;
    for (const auto& frame : frames) {
        LT_CHECK(frame.stream != 1u);
        if (frame.type == 8 && frame.stream == 3) ++grants;
    }
    LT_CHECK_EQ(grants, 1u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 1u);
LT_END_AUTO_TEST(reset_discards_unexposed_grants_preserving_borrowed_head_and_sibling)
LT_BEGIN_AUTO_TEST(http2_reset_suite, closed_stream_traffic_preserves_hpack_and_refunds_data_without_reset_echo)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok()); unsigned calls = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> { ++calls; x.respond(http::status::from_code(204), {}); co_return; }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    // Only an endpoint that SENT reset must discard in-flight frames (RFC 9113 5.1).
    auto opening = h2test::get(); opening.push_back({":method", "GET"});
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, opening)));
    LT_ASSERT(h2test::feed(engine, wire));
    executor.run_pending(); LT_CHECK_EQ(calls, 0u);
    const auto initial_resets = h2test::resets(h2test::output(engine));
    LT_ASSERT_EQ(initial_resets.size(), 1u); LT_CHECK_EQ(initial_resets[0].code, 1u);
    for (unsigned i = 0; i < 3; ++i) {
        LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 1, {'a', 'b'})));
    }
    auto fields = h2test::get(); fields.push_back({"x-state", "inserted-after-reset"});
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, fields))));
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 3, h2test::encode(encoder, fields)))); executor.run_pending();
    auto out = h2test::output(engine); LT_CHECK_EQ(h2test::resets(out).size(), 0u); LT_CHECK_EQ(calls, 1u);
    auto replies = h2test::responses(out); LT_ASSERT_EQ(replies.size(), 1u); LT_CHECK_EQ(replies[0].stream, 3u);
    unsigned reclaimed = 0; for (const auto& frame : h2test::frames(out)) if (frame.type == 8 && frame.stream == 0) {
        reclaimed += h2test::read_u32(frame.payload.data());
    }
    LT_CHECK_EQ(reclaimed, 6u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
LT_END_AUTO_TEST(closed_stream_traffic_preserves_hpack_and_refunds_data_without_reset_echo)
LT_BEGIN_AUTO_TEST(http2_reset_suite, local_abort_cancels_once_and_defers_coroutine_destruction_until_resume_returns)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause; httpserver::stop_token token; unsigned destroyed = 0, after = 0;
    struct lifetime { unsigned& destroyed; ~lifetime() { ++destroyed; } };
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        lifetime guard{destroyed}; token = x.cancellation();
        LT_CHECK(x.abort().ok()); LT_CHECK(token.stop_requested()); LT_CHECK_EQ(destroyed, 0u);
        x.abort(); co_await pause.wait(); ++after;
    }).ok());
    httpserver::manual_executor executor;
    {
        hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); LT_CHECK_EQ(destroyed, 0u);
        auto resets = h2test::resets(h2test::output(engine)); LT_CHECK_EQ(resets.size(), 1u);
        LT_CHECK_EQ(destroyed, 1u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
        pause.signal(); executor.run_pending(); LT_CHECK_EQ(after, 0u); LT_CHECK(engine.output().empty());
        LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, h2test::increment(8)))); LT_CHECK(engine.output().empty());
    }
    for (auto kind : {hs::resource::streams, hs::resource::body_buffer_bytes, hs::resource::header_bytes,
        hs::resource::header_fields, hs::resource::response_queue_bytes}) LT_CHECK_EQ(budget.in_use(kind), 0u);
LT_END_AUTO_TEST(local_abort_cancels_once_and_defers_coroutine_destruction_until_resume_returns)
LT_BEGIN_AUTO_TEST(http2_reset_suite, peer_reset_invalidates_queued_and_parked_handlers_and_returns_reservations)
    for (bool start : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::resume_signal pause; httpserver::stop_token token; unsigned calls = 0, after = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            ++calls; token = x.cancellation(); co_await pause.wait(); ++after;
        }).ok());
        httpserver::manual_executor executor;
        {
            hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
            auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
            LT_ASSERT(h2test::feed(engine, wire)); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 1u);
            LT_CHECK(budget.in_use(hs::resource::header_bytes) > 0u);
            if (start) executor.run_pending();
            LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, h2test::increment(8))));
            if (start) {
                LT_CHECK(token.stop_requested());
            }
            pause.signal(); executor.run_pending(); LT_CHECK_EQ(after, 0u); LT_CHECK_EQ(calls, start ? 1u : 0u);
            LT_CHECK_EQ(h2test::resets(h2test::output(engine)).size(), 0u);
            LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u); LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 0u);
        }
        for (auto kind : {hs::resource::streams, hs::resource::body_buffer_bytes, hs::resource::header_bytes,
            hs::resource::header_fields, hs::resource::response_queue_bytes}) LT_CHECK_EQ(budget.in_use(kind), 0u);
    }
LT_END_AUTO_TEST(peer_reset_invalidates_queued_and_parked_handlers_and_returns_reservations)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
