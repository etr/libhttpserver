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
LT_BEGIN_SUITE(http2_streaming_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_streaming_suite)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, incremental_body_dispatches_before_end_and_credits_only_consumption)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    std::string received; unsigned calls = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        ++calls; x.admit_body({4}); std::array<std::byte, 2> bytes;
        for (;;) {
            auto part = co_await x.body().read_some(bytes);
            if (!part.status.ok() || part.end_of_body) break;
            received.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
        }
        x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto fields = h2test::get(); fields[0].value = "POST";
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); LT_CHECK_EQ(calls, 1u);
    h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 1, {'a', 'b'})));
    LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 8), 0u);
    executor.run_pending(); LT_CHECK_EQ(received, "ab");
    LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 8), 1u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 1, 1, {'c', 'd'})));
    executor.run_pending(); LT_CHECK_EQ(received, "abcd");
    LT_CHECK_EQ(h2test::responses(h2test::output(engine)).size(), 1u);
LT_END_AUTO_TEST(incremental_body_dispatches_before_end_and_credits_only_consumption)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, streaming_head_is_open_and_data_finishes_exact_body)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    bool written = false, finished = false;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        std::string body = "streamed";
        written = (co_await x.writer().write(std::as_bytes(std::span(body)))).status.ok();
        finished = (co_await x.writer().finish()).status.ok();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); h2test::output(engine); executor.run_pending();
    auto output = h2test::output(engine);
    LT_CHECK(written); LT_CHECK(finished); LT_CHECK_EQ(h2test::resets(output).size(), 0u);
    LT_ASSERT(output.size() >= 9); LT_CHECK_EQ(output[3], 1u); LT_CHECK_EQ(output[4] & 1, 0u);
    LT_CHECK_EQ(h2test::count_type(output, 0), 2u);
    std::string actual;
    auto frames = h2test::frames(output);
    for (const auto& frame : frames) if (frame.type == 0) actual.append(frame.payload.begin(), frame.payload.end());
    LT_CHECK_EQ(actual, "streamed"); LT_CHECK_EQ(frames.back().flags, 1u);
LT_END_AUTO_TEST(streaming_head_is_open_and_data_finishes_exact_body)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, stream_window_stall_does_not_block_sibling_or_ping)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        std::string body = "abcdef";
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(4, 0)));
    for (auto id : {1u, 3u}) h2test::append(wire, h2test::frame(1, 5, id, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 3, h2test::increment(6))));
    LT_ASSERT(h2test::feed(engine, h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8, 9))));
    auto first = h2test::frames(h2test::output(engine, 1));
    LT_ASSERT(first.size() >= 2); LT_CHECK_EQ(first[0].type, 6u);
    std::string body;
    for (const auto& f : first) if (f.type == 0) {
        LT_CHECK_EQ(f.stream, 3u);
        body.append(f.payload.begin(), f.payload.end());
    }
    LT_CHECK_EQ(body, "abcdef");
    LT_ASSERT(h2test::feed(engine, h2test::frame(8, 0, 1, h2test::increment(6))));
    body.clear();
    for (const auto& f : h2test::frames(h2test::output(engine, 1))) if (f.type == 0) {
        LT_CHECK_EQ(f.stream, 1u);
        body.append(f.payload.begin(), f.payload.end());
    }
    LT_CHECK_EQ(body, "abcdef");
LT_END_AUTO_TEST(stream_window_stall_does_not_block_sibling_or_ping)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, paused_reader_does_not_hold_parser_or_credit)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause; unsigned completed = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({8}); if (x.head().head_fields.first("x-pause")) {
            co_await pause.wait();
        }
        auto body = co_await x.body().collect(8); if (body.status.ok()) {
            ++completed;
        }
        x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface(); auto fields = h2test::get(); fields[0].value = "POST";
    fields.push_back({"x-pause", "yes"}); h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    fields.pop_back(); h2test::append(wire, h2test::frame(1, 4, 3, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
    for (auto id : {1u, 3u}) LT_ASSERT(h2test::feed(engine, h2test::frame(0, 1, id, {'x', 'y'})));
    LT_ASSERT(h2test::feed(engine, h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8))));
    executor.run_pending(); LT_CHECK_EQ(completed, 1u);
    auto out = h2test::output(engine); LT_CHECK_EQ(h2test::count_type(out, 6), 1u);
    LT_ASSERT_EQ(h2test::responses(out).size(), 1u); LT_CHECK_EQ(h2test::responses(out)[0].stream, 3u);
    pause.signal(); executor.run_pending(); LT_CHECK_EQ(completed, 2u);
LT_END_AUTO_TEST(paused_reader_does_not_hold_parser_or_credit)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, request_and_response_trailers_follow_data_in_hpack_order)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned received = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({8}); LT_CHECK_EQ(x.body().trailers().size(), 0u);
        auto body = co_await x.body().collect(8);
        if (body.status.ok() && body.data.size() == 3 && x.body().trailers().count("x-last") == 2) ++received;
        x.start_response(http::status::from_code(200), {});
        std::string answer = "reply"; co_await x.writer().write(std::as_bytes(std::span(answer)));
        http::fields trailers; trailers.append("X-End", "one"); trailers.append("X-End", "two");
        co_await x.writer().finish(trailers);
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface(); auto fields = h2test::get(); fields[0].value = "POST";
    fields.push_back({"content-length", "3"}); fields.push_back({"content-length", "003"});
    h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    h2test::append(wire, h2test::frame(0, 8, 1, {2, 'a', 'b', 'c', 0, 0}));
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, {{"x-last", "one"}, {"x-last", "two"}})));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); LT_CHECK_EQ(received, 1u);
    auto output = h2test::output(engine); LT_CHECK_EQ(h2test::resets(output).size(), 0u);
    auto sections = h2test::responses(output, true); LT_ASSERT_EQ(sections.size(), 2u);
    LT_CHECK(!sections[0].end_stream); LT_CHECK(sections[1].end_stream);
    LT_ASSERT_EQ(sections[1].fields.size(), 2u); LT_CHECK_EQ(sections[1].fields[1].value, "two");
LT_END_AUTO_TEST(request_and_response_trailers_follow_data_in_hpack_order)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, full_queue_writer_parks_and_peer_reset_releases_resources)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned completed = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        std::string body(40000, 'w'); co_await x.writer().write(std::as_bytes(std::span(body)));
        ++completed; co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor;
    {
        hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
        h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(4, 0)));
        h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); LT_CHECK_EQ(completed, 0u);
        LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, {0, 0, 0, 8})));
        executor.run_pending(); LT_CHECK_EQ(completed, 0u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    }
    for (auto r : {hs::resource::body_buffer_bytes, hs::resource::response_queue_bytes, hs::resource::streams, hs::resource::header_bytes}) LT_CHECK_EQ(budget.in_use(r), 0u);
LT_END_AUTO_TEST(full_queue_writer_parks_and_peer_reset_releases_resources)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, granted_data_and_window_relief_survive_unavailable_shared_body_budget)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    std::size_t received = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({4}); auto body = co_await x.body().collect(4);
        if (body.status.ok()) {
            received = body.data.size();
        }
        x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto fields = h2test::get(); fields[0].value = "POST"; auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
    hs::reservation held;
    LT_ASSERT(budget.reserve(hs::resource::body_buffer_bytes, budget.capacity(hs::resource::body_buffer_bytes) - budget.in_use(hs::resource::body_buffer_bytes), held).ok());
    LT_CHECK(h2test::feed(engine, h2test::frame(8, 0, 1, h2test::increment(1))));
    LT_CHECK(h2test::feed(engine, h2test::frame(0, 1, 1, {'a', 'b', 'c', 'd'})));
    executor.run_pending(); LT_CHECK_EQ(received, 4u); LT_CHECK(!engine.failure());
LT_END_AUTO_TEST(granted_data_and_window_relief_survive_unavailable_shared_body_budget)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, malformed_request_lengths_and_trailers_reset_only_their_stream)
    for (unsigned variant = 0; variant < 6; ++variant) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        unsigned valid = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [](exchange& x) -> task<void> {
            x.admit_body({8}); co_await x.body().collect(8); x.respond(http::status::from_code(204), {});
        }).ok());
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> { ++valid; x.respond(http::status::from_code(204), {}); co_return; }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        auto wire = h2test::preface(); auto fields = h2test::get(); fields[0].value = "POST";
        if (variant == 0) {
            fields.push_back({"content-length", "1"}); fields.push_back({"content-length", "2"});
        }
        if (variant == 1) fields.push_back({"content-length", "18446744073709551616"});
        if (variant == 2 || variant == 3) fields.push_back({"content-length", "2"});
        h2test::append(wire, h2test::frame(1, variant == 2 ? 5 : 4, 1, h2test::encode(encoder, fields)));
        if (variant == 3) h2test::append(wire, h2test::frame(0, 1, 1, {'a'}));
        if (variant >= 4) h2test::append(wire, h2test::frame(1, variant == 4 ? 4 : 5, 1, h2test::encode(encoder, {{variant == 4 ? "x-end" : ":path", "bad"}})));
        h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); auto out = h2test::output(engine);
        auto resets = h2test::resets(out); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].stream, 1u); LT_CHECK_EQ(resets[0].code, 1u);
        LT_CHECK_EQ(valid, 1u); LT_CHECK(!engine.failure()); LT_ASSERT_EQ(h2test::responses(out).size(), 1u); LT_CHECK_EQ(h2test::responses(out)[0].stream, 3u);
    }
LT_END_AUTO_TEST(malformed_request_lengths_and_trailers_reset_only_their_stream)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, response_length_and_forbidden_body_failures_are_stream_scoped)
    for (unsigned variant = 0; variant < 4; ++variant) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        bool failed = false;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            if (x.head().head_fields.first("x-bad")) {
                http::fields fields; fields.append("content-length", variant == 1 ? "3" : "1");
                x.start_response(http::status::from_code(variant >= 2 ? 204 : 200), fields);
                if (variant == 3) {
                    failed = !(co_await x.writer().finish()).status.ok();
                    co_return;
                }
                std::string body = "xx";
                auto write = co_await x.writer().write(std::as_bytes(std::span(body)));
                auto finish = co_await x.writer().finish(); failed = !write.status.ok() || !finish.status.ok();
            } else {
                x.respond(http::status::from_code(204), {});
            }
        }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        auto fields = h2test::get(); fields.push_back({"x-bad", "yes"}); auto wire = h2test::preface();
        h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, fields)));
        h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); auto out = h2test::output(engine);
        LT_CHECK(failed); LT_CHECK(!engine.failure()); auto resets = h2test::resets(out); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].stream, 1u); LT_CHECK_EQ(resets[0].code, 2u);
        LT_ASSERT_EQ(h2test::responses(out).size(), 1u); LT_CHECK_EQ(h2test::responses(out)[0].stream, 3u);
    }
LT_END_AUTO_TEST(response_length_and_forbidden_body_failures_are_stream_scoped)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, empty_finish_is_not_blocked_by_zero_send_windows)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {}); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(4, 0)));
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); auto out = h2test::frames(h2test::output(engine));
    LT_ASSERT(!out.empty()); LT_CHECK_EQ(out.back().type, 0u); LT_CHECK_EQ(out.back().flags, 1u); LT_CHECK(out.back().payload.empty());
LT_END_AUTO_TEST(empty_finish_is_not_blocked_by_zero_send_windows)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, tiny_admitted_queue_streams_a_larger_body_with_exact_credit)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    std::string received;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({2}); std::array<std::byte, 1> into;
        for (;;) {
            auto part = co_await x.body().read_some(into);
            if (!part.status.ok() || part.end_of_body) break;
            received.append(reinterpret_cast<const char*>(part.data.data()), part.data.size());
        }
        x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    LT_ASSERT(h2test::feed(engine, h2test::preface())); h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));
    auto fields = h2test::get(); fields[0].value = "POST"; fields.push_back({"content-length", "10"});
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)))); executor.run_pending(); h2test::output(engine);
    std::size_t connection_credit = 0, stream_credit = 0;
    for (unsigned i = 0; i < 5; ++i) {
        LT_ASSERT(h2test::feed(engine, h2test::frame(0, i == 4 ? 1 : 0, 1, {'a', 'b'})));
        executor.run_pending();
        for (const auto& frame : h2test::frames(h2test::output(engine))) if (frame.type == 8) {
            auto n = h2test::read_u32(frame.payload.data());
            if (frame.stream) {
                stream_credit += n;
            } else {
                connection_credit += n;
            }
        }
    }
    LT_CHECK_EQ(received, "ababababab"); LT_CHECK_EQ(connection_credit, 10u); LT_CHECK_EQ(stream_credit, 8u); LT_CHECK(!engine.failure());
LT_END_AUTO_TEST(tiny_admitted_queue_streams_a_larger_body_with_exact_credit)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, parked_body_reader_is_invalidated_by_peer_reset)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned resumed = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({2}); co_await x.body().collect(8); ++resumed;
    }).ok());
    httpserver::manual_executor executor;
    {
        hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
        auto fields = h2test::get(); fields[0].value = "POST"; auto wire = h2test::preface();
        h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
        LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, {0, 0, 0, 8})));
        executor.run_pending(); LT_CHECK_EQ(resumed, 0u); LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    }
    executor.run_pending(); LT_CHECK_EQ(resumed, 0u);
    for (auto r : {hs::resource::body_buffer_bytes, hs::resource::response_queue_bytes, hs::resource::streams, hs::resource::header_bytes}) LT_CHECK_EQ(budget.in_use(r), 0u);
LT_END_AUTO_TEST(parked_body_reader_is_invalidated_by_peer_reset)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, reset_preserves_borrowed_partial_data_and_sibling_progress)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {}); std::string body = "abc";
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface(); for (auto id : {1u, 3u}) h2test::append(wire, h2test::frame(1, 5, id, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); h2test::output(engine); executor.run_pending();
    for (unsigned i = 0; i < 2; ++i) {
        auto head = engine.output();
        LT_ASSERT_EQ(head[3], 1u);
        LT_ASSERT(engine.advance_output(head.size()));
    }
    auto borrowed = engine.output(); LT_ASSERT_EQ(borrowed[3], 0u); auto saved = std::vector<std::uint8_t>(borrowed.begin(), borrowed.end());
    LT_ASSERT(engine.advance_output(2)); LT_ASSERT(h2test::feed(engine, h2test::frame(3, 0, 1, {0, 0, 0, 8})));
    LT_CHECK(std::equal(borrowed.begin(), borrowed.end(), saved.begin()));
    std::vector<std::uint8_t> out(saved.begin(), saved.begin() + 2); h2test::append(out, h2test::output(engine, 1));
    auto frames = h2test::frames(out); LT_ASSERT_EQ(frames.size(), 3u);
    LT_CHECK_EQ(frames[0].stream, 1u); LT_CHECK_EQ(frames[1].stream, 3u); LT_CHECK_EQ(frames[2].flags, 1u); LT_CHECK(!engine.failure());
LT_END_AUTO_TEST(reset_preserves_borrowed_partial_data_and_sibling_progress)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, invalid_body_queue_limits_fail_before_dispatch)
    for (bool response : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits;
        if (response) {
            limits.response_buffer_bytes = 0;
        } else {
            limits.body_buffer_bytes = 0;
        }
        hd::http2_request_engine engine(budget, routes, executor, limits);
        LT_ASSERT(engine.failure()); LT_CHECK(engine.failure()->outcome == http::outcome_code::invalid_argument);
    }
LT_END_AUTO_TEST(invalid_body_queue_limits_fail_before_dispatch)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, empty_receive_end_survives_negative_window_after_settings_ack)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause; std::size_t received = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({4}); co_await pause.wait(); auto body = co_await x.body().collect(4);
        if (body.status.ok()) received = body.data.size();
        x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto fields = h2test::get(); fields[0].value = "POST"; auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 1, {'a', 'b'})));
    LT_ASSERT(h2test::feed(engine, h2test::frame(4, 1)));
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 1, 1)));
    pause.signal(); executor.run_pending(); LT_CHECK_EQ(received, 2u);
    auto out = h2test::output(engine); LT_CHECK_EQ(h2test::resets(out).size(), 0u); LT_CHECK_EQ(h2test::responses(out).size(), 1u);
LT_END_AUTO_TEST(empty_receive_end_survives_negative_window_after_settings_ack)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, output_allocation_admission_failure_publishes_terminal_control_without_another_event)
    hs::budget_limits capacities; capacities.set(hs::resource::response_queue_bytes, 25000);
    auto budget = hs::resource_budget::root(capacities); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {}); std::string body(16384, 'b');
        co_await x.writer().write(std::as_bytes(std::span(body))); co_await x.writer().finish();
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); auto out = h2test::output(engine);
    LT_ASSERT(engine.failure()); LT_CHECK_EQ(h2test::count_type(out, 7), 1u);
LT_END_AUTO_TEST(output_allocation_admission_failure_publishes_terminal_control_without_another_event)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, completed_early_body_exceeding_admission_policy_resets_only_its_stream)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    bool failed = false;
    LT_ASSERT(routes.route(http::method::known(http::method_id::post), "/hello", [&](exchange& x) -> task<void> {
        x.admit_body({2}); auto body = co_await x.body().collect(8); failed = !body.status.ok();
        x.respond(http::status::from_code(204), {});
    }).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> { x.respond(http::status::from_code(204), {}); co_return; }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto fields = h2test::get(); fields[0].value = "POST"; auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, fields)));
    h2test::append(wire, h2test::frame(0, 1, 1, {'a', 'b', 'c'}));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); auto out = h2test::output(engine);
    LT_CHECK(failed); LT_CHECK(!engine.failure()); auto resets = h2test::resets(out); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].stream, 1u); LT_CHECK_EQ(resets[0].code, 7u);
    LT_ASSERT_EQ(h2test::responses(out).size(), 1u); LT_CHECK_EQ(h2test::responses(out)[0].stream, 3u);
LT_END_AUTO_TEST(completed_early_body_exceeding_admission_policy_resets_only_its_stream)
LT_BEGIN_AUTO_TEST(http2_streaming_suite, data_after_receive_end_uses_stream_closed_and_preserves_sibling)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal pause;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        if (x.head().head_fields.first("x-pause")) co_await pause.wait();
        x.respond(http::status::from_code(204), {});
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor); hd::hpack_encoder encoder(budget);
    auto fields = h2test::get(); fields.push_back({"x-pause", "yes"}); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, fields)));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); auto sibling = h2test::responses(h2test::output(engine)); LT_ASSERT_EQ(sibling.size(), 1u); LT_CHECK_EQ(sibling[0].stream, 3u);
    LT_ASSERT(h2test::feed(engine, h2test::frame(0, 0, 1, {'a'})));
    auto resets = h2test::resets(h2test::output(engine)); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].stream, 1u); LT_CHECK_EQ(resets[0].code, 5u); LT_CHECK(!engine.failure());
LT_END_AUTO_TEST(data_after_receive_end_uses_stream_closed_and_preserves_sibling)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
