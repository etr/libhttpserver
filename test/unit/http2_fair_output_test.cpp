/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <string>
#include <vector>
#include <httpserver/exchange.hpp>
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::exchange;
using httpserver::task;
LT_BEGIN_SUITE(http2_fair_output_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_fair_output_suite)
LT_BEGIN_AUTO_TEST(http2_fair_output_suite, large_peer_frames_still_get_bounded_round_robin_data)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/large", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        std::string bytes(40000, 'a'); co_await x.writer().write(std::as_bytes(std::span(bytes))); co_await x.writer().finish();
    }).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/tiny", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        std::string bytes = "tiny"; co_await x.writer().write(std::as_bytes(std::span(bytes))); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_limits limits; limits.response_buffer_bytes = 65535;
    hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(5, 65535)));
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get("/large"))));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get("/tiny"))));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
    const auto frames = h2test::frames(h2test::output(engine, 3));
    std::string large, tiny; bool tiny_finished_before_large = false;
    for (const auto& frame : frames) if (frame.type == 0) {
        LT_CHECK(frame.payload.size() <= 16384u);
        if (frame.stream == 1) large.append(frame.payload.begin(), frame.payload.end());
        if (frame.stream == 3) {
            tiny.append(frame.payload.begin(), frame.payload.end());
            if (frame.flags & 1) tiny_finished_before_large = large.size() < 40000;
        }
    }
    LT_CHECK_EQ(large, std::string(40000, 'a')); LT_CHECK_EQ(tiny, "tiny"); LT_CHECK(tiny_finished_before_large);
LT_END_AUTO_TEST(large_peer_frames_still_get_bounded_round_robin_data)
LT_BEGIN_AUTO_TEST(http2_fair_output_suite, replenished_controls_and_headers_cannot_starve_ready_data)
    for (bool controls : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes;
        LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/body", [](exchange& x) -> task<void> {
            x.start_response(http::status::from_code(200), {}); std::string bytes(20000, 'b');
            co_await x.writer().write(std::as_bytes(std::span(bytes))); co_await x.writer().finish();
        }).ok());
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/empty", [](exchange& x) -> task<void> { x.respond(http::status::from_code(204), {}); co_return; }).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits; limits.response_buffer_bytes = 32768;
        hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine);
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get("/body"))))); executor.run_pending();
        auto head = engine.output(); LT_ASSERT_EQ(head[3], 1u); LT_ASSERT(engine.advance_output(head.size()));
        unsigned data = 0, other = 0;
        for (unsigned i = 0; i < 20; ++i) {
            auto incoming = controls ? h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8, 0)) : h2test::frame(1, 5, 3 + 2 * i, h2test::encode(encoder, h2test::get("/empty")));
            LT_ASSERT(h2test::feed(engine, incoming)); executor.run_pending();
            auto item = engine.output(); LT_ASSERT(!item.empty());
            if (item[3] == 0) {
                ++data;
            } else {
                ++other;
            }
            if (!data) LT_CHECK(i < 8u);
            std::vector<std::uint8_t> saved(item.begin(), item.end()); LT_ASSERT(engine.advance_output(1));
            auto rest = engine.output(); LT_CHECK(std::equal(rest.begin(), rest.end(), saved.begin() + 1)); LT_ASSERT(engine.advance_output(rest.size()));
        }
        LT_CHECK(data >= 2u); LT_CHECK(other > 0u); LT_CHECK(!engine.failure());
    }
LT_END_AUTO_TEST(replenished_controls_and_headers_cannot_starve_ready_data)
LT_BEGIN_AUTO_TEST(http2_fair_output_suite, sustained_controls_allow_bounded_semantic_output_across_rate_refills)
    using namespace std::chrono_literals;  // NOLINT(build/namespaces)
    for (bool reset : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes;
        LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/empty", [reset](exchange& x) -> task<void> {
            if (reset) {
                x.abort();
            } else {
                x.respond(http::status::from_code(204), {});
            }
            co_return;
        }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        LT_ASSERT(h2test::feed(engine, h2test::preface()));
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get("/empty"))))); executor.run_pending();
        auto initial = engine.output(); LT_ASSERT(!initial.empty()); LT_CHECK_EQ(initial[3], 4u); LT_CHECK_EQ(initial[4], 0u);
        LT_ASSERT(engine.advance_output(initial.size()));
        LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));  // retire the initial SETTINGS deadline
        unsigned semantic = 0, controls = 0;
        std::vector<std::uint8_t> wire;
        for (unsigned i = 0; i < 300; ++i) {
            const auto now = hd::http2_connection::time_point {} + 10ms * i;
            if (i == 100 || i == 200) {
                const auto id = 1 + 2 * (i / 100);
                LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, id, h2test::encode(encoder, h2test::get("/empty"))), now));
                executor.run_pending();
            }
            LT_ASSERT(h2test::feed(engine, h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8)), now));
            auto item = engine.output(now); LT_ASSERT(!item.empty());
            if (item[3] == (reset ? 3 : 1)) ++semantic;
            if (item[3] == 6) ++controls;
            std::vector<std::uint8_t> saved(item.begin(), item.end()); h2test::append(wire, saved);
            LT_ASSERT(engine.advance_output(1));
            auto rest = engine.output(now); LT_CHECK(std::equal(rest.begin(), rest.end(), saved.begin() + 1)); LT_ASSERT(engine.advance_output(rest.size()));
            if (i % 100 == 9) LT_CHECK_EQ(semantic, i / 100 + 1);
        }
        LT_CHECK_EQ(semantic, 3u); LT_CHECK(controls > 280u); LT_CHECK(!engine.failure());
        LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
        if (reset) {
            const auto resets = h2test::resets(wire); LT_CHECK_EQ(resets.size(), 3u);
            for (unsigned i = 0; i < resets.size(); ++i) {
                LT_CHECK_EQ(resets[i].stream, 1 + 2 * i); LT_CHECK_EQ(resets[i].code, 2u);
            }
        } else {
            const auto responses = h2test::responses(wire); LT_CHECK_EQ(responses.size(), 3u);
            for (unsigned i = 0; i < responses.size(); ++i) {
                LT_CHECK_EQ(responses[i].stream, 1 + 2 * i); LT_CHECK_EQ(responses[i].fields[0].value, "204");
            }
        }
    }
LT_END_AUTO_TEST(sustained_controls_allow_bounded_semantic_output_across_rate_refills)
LT_BEGIN_AUTO_TEST(http2_fair_output_suite, quantum_storage_remains_charged_until_retirement_and_reset_returns_to_baseline)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {}); std::string body(40000, 'q');
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_limits limits; limits.response_buffer_bytes = 65535;
    {
        hd::http2_request_engine engine(budget, routes, executor, limits); hd::hpack_encoder encoder(budget);
        const auto baseline_body = budget.in_use(hs::resource::body_buffer_bytes);
        const auto baseline_output = budget.in_use(hs::resource::response_queue_bytes);
        auto wire = h2test::preface(); h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(5, 65535)));
        h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); h2test::output(engine); executor.run_pending();
        auto head = engine.output(); LT_ASSERT_EQ(head[3], 1u); LT_ASSERT(engine.advance_output(head.size()));
        auto data = engine.output(); LT_ASSERT_EQ(data[3], 0u); LT_ASSERT_EQ(data.size(), 16384u + 9u);
        const auto held = budget.in_use(hs::resource::response_queue_bytes);
        LT_CHECK(held <= budget.capacity(hs::resource::response_queue_bytes)); LT_CHECK(held > baseline_output);
        LT_ASSERT(engine.advance_output(1)); LT_CHECK_EQ(budget.in_use(hs::resource::response_queue_bytes), held);
        LT_ASSERT(engine.advance_output(data.size() - 1));
        LT_CHECK_EQ(budget.in_use(hs::resource::response_queue_bytes), held - (2 * 16384 + 9 + 128));
        LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, h2test::increment(8)))); LT_CHECK(engine.output().empty());
        LT_CHECK_EQ(budget.in_use(hs::resource::body_buffer_bytes), baseline_body);
        LT_CHECK_EQ(budget.in_use(hs::resource::response_queue_bytes), baseline_output);
        LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u); LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 0u);
        LT_CHECK_EQ(budget.in_use(hs::resource::header_fields), 0u);
    }
    for (auto kind : {hs::resource::streams, hs::resource::body_buffer_bytes, hs::resource::header_bytes,
        hs::resource::header_fields, hs::resource::response_queue_bytes}) LT_CHECK_EQ(budget.in_use(kind), 0u);
LT_END_AUTO_TEST(quantum_storage_remains_charged_until_retirement_and_reset_returns_to_baseline)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
