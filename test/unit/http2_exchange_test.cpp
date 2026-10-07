/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/http1_head_parser.hpp>
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::task;
using httpserver::exchange;
LT_BEGIN_SUITE(http2_exchange_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_exchange_suite)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, concurrent_gets_share_http1_routing_without_head_of_line_blocking)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal parked;
    std::vector<std::string> seen;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        seen.push_back(std::string(x.head().request_method.name()) + x.head().route_path);
        if (x.head().head_fields.first("x-park")) co_await parked.wait();
        http::fields f; f.append("X-Reply", "same");
        x.respond(http::status::from_code(204), f); co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto first = h2test::get(); first.push_back({"x-park", "yes"});
    auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, first)));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
    auto replies = h2test::responses(h2test::output(engine));
    LT_ASSERT_EQ(replies.size(), 1u); LT_CHECK_EQ(replies[0].stream, 3u);
    LT_CHECK_EQ(replies[0].fields[1].name, "x-reply");
    parked.signal(); executor.run_pending();
    auto last = h2test::responses(h2test::output(engine));
    // Each independent capture uses a fresh peer decoder, so the repeated
    // response field uses without-indexing in the engine's response policy.
    LT_ASSERT_EQ(last.size(), 1u); LT_CHECK_EQ(last[0].stream, 1u);
    hd::recording_sink sink;
    hd::http1_head_parser parser(hd::http1_head_budget{});
    parser.feed("GET /items/../hello?x=1 HTTP/1.1\r\nHost: example.test\r\n\r\n");
    LT_ASSERT(parser.state() == hd::http1_head_state::complete);
    auto head = parser.take();
    exchange x(head, &sink);
    httpserver::spawn(executor, hd::run_route(routes, x), [](httpserver::task_result<void>) {});
    executor.run_pending(); LT_CHECK_EQ(sink.respond_code, 204u);
    LT_CHECK_EQ(seen.size(), 3u); LT_CHECK(seen[0] == seen[1] && seen[1] == seen[2]);
LT_END_AUTO_TEST(concurrent_gets_share_http1_routing_without_head_of_line_blocking)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, route_boundary_synthesizes_errors_and_rejects_body_requests)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned calls = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
            ++calls;
            throw std::runtime_error("failure");
            co_return;
        }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 4, 1, h2test::encode(encoder, h2test::get())));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get("/missing"))));
    h2test::append(wire, h2test::frame(1, 5, 5, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); LT_CHECK_EQ(calls, 1u);
    auto output = h2test::output(engine); LT_CHECK_EQ(h2test::count_type(output, 3), 1u);
    auto replies = h2test::responses(output); LT_ASSERT_EQ(replies.size(), 2u);
    LT_CHECK_EQ(replies[0].fields[0].value, "404"); LT_CHECK_EQ(replies[1].fields[0].value, "500");
LT_END_AUTO_TEST(route_boundary_synthesizes_errors_and_rejects_body_requests)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, bounded_stream_admission_preserves_dynamic_table_and_releases_on_destruction)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal parked; unsigned calls = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        ++calls;
        if (x.head().head_fields.first("x-park")) co_await parked.wait();
        x.respond(http::status::from_code(204), {}); co_return;
    }).ok());
    httpserver::manual_executor executor;
    {
        hd::http2_request_limits limits; limits.max_streams = 1;
        hd::http2_request_engine engine(budget, routes, executor, limits);
        hd::hpack_encoder encoder(budget); auto fields = h2test::get(); fields.push_back({"x-park", "yes"});
        auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, fields)));
        fields = h2test::get(); fields.push_back({"x-inserted", "refused-block"});
        h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, fields)));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); LT_CHECK_EQ(calls, 1u);
        LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 3), 1u);
        parked.signal(); executor.run_pending(); h2test::output(engine);
        LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 5, h2test::encode(encoder, fields))));
        executor.run_pending(); LT_CHECK_EQ(calls, 2u);
    }
    executor.run_pending();
    for (auto kind : {hs::resource::streams, hs::resource::header_bytes, hs::resource::header_fields, hs::resource::response_queue_bytes, hs::resource::body_buffer_bytes}) {
        LT_CHECK_EQ(budget.in_use(kind), 0u);
    }
LT_END_AUTO_TEST(bounded_stream_admission_preserves_dynamic_table_and_releases_on_destruction)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, compressed_and_expanded_limits_fail_before_dispatch)
    for (bool compressed : {true, false}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_limits limits;
        if (compressed) {
            limits.headers.max_compressed_bytes = 2;
        } else {
            limits.headers.max_expanded_bytes = 80;
        }
        {
            hd::http2_request_engine engine(budget, routes, executor, limits);
            hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
            h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
            LT_CHECK(!h2test::feed(engine, wire)); LT_ASSERT(engine.failure());
            LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 7), 1u);
        }
        LT_CHECK_EQ(budget.in_use(hs::resource::header_bytes), 0u);
    }
LT_END_AUTO_TEST(compressed_and_expanded_limits_fail_before_dispatch)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, response_blocks_fragment_and_preserve_borrowed_output_across_failure)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields; fields.append("X-Large", std::string(20000, 'z'));
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); h2test::output(engine); executor.run_pending();
    auto borrowed = engine.output(); LT_ASSERT(!borrowed.empty());
    auto saved = std::vector<std::uint8_t>(borrowed.begin(), borrowed.end());
    LT_ASSERT(engine.advance_output(2));
    LT_CHECK(!h2test::feed(engine, h2test::frame(9, 4, 3)));
    LT_CHECK(std::equal(borrowed.begin(), borrowed.end(), saved.begin()));
    std::vector<std::uint8_t> output{saved.begin(), saved.begin() + 2};
    h2test::append(output, h2test::output(engine, 7));
    LT_CHECK_EQ(h2test::count_type(output, 9), 1u); LT_CHECK_EQ(h2test::count_type(output, 7), 1u);
    auto replies = h2test::responses(output); LT_ASSERT_EQ(replies.size(), 1u);
    LT_CHECK_EQ(replies[0].fields[1].value.size(), 20000u);
LT_END_AUTO_TEST(response_blocks_fragment_and_preserve_borrowed_output_across_failure)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, peer_header_limit_refuses_one_response_without_poisoning_encoder)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields fields;
        if (x.head().head_fields.first("x-large")) fields.append("x-large", std::string(100, 'z'));
        x.respond(http::status::from_code(200), fields); co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(6, 50)));
    auto large = h2test::get(); large.push_back({"x-large", "yes"});
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, large)));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
    auto output = h2test::output(engine);
    LT_CHECK(!engine.failure()); LT_CHECK_EQ(h2test::count_type(output, 3), 1u);
    auto replies = h2test::responses(output); LT_ASSERT_EQ(replies.size(), 1u); LT_CHECK_EQ(replies[0].stream, 3u);
LT_END_AUTO_TEST(peer_header_limit_refuses_one_response_without_poisoning_encoder)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, response_queue_refusal_during_handler_is_safe)
    hs::budget_limits capacities; capacities.set(hs::resource::response_queue_bytes, 4096);
    auto budget = hs::resource_budget::root(capacities); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned finished = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.respond(http::status::from_code(200), {}); ++finished; co_return;
    }).ok());
    httpserver::manual_executor executor;
    {
        hd::http2_request_engine engine(budget, routes, executor);
        hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
        h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
        LT_CHECK_EQ(finished, 1u); LT_ASSERT(engine.failure());
        LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 7), 1u);
    }
    LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
    LT_CHECK_EQ(budget.in_use(hs::resource::response_queue_bytes), 0u);
LT_END_AUTO_TEST(response_queue_refusal_during_handler_is_safe)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, destruction_cancels_owned_suspended_and_queued_handler_frames)
    for (bool start : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::resume_signal parked; unsigned after = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
            co_await parked.wait(); ++after; co_return;
        }).ok());
        httpserver::manual_executor executor;
        {
            hd::http2_request_engine engine(budget, routes, executor);
            hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
            h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
            LT_ASSERT(h2test::feed(engine, wire));
            if (start) executor.run_pending();
        }
        parked.signal(); executor.run_pending(); LT_CHECK_EQ(after, 0u);
        for (auto kind : {hs::resource::streams, hs::resource::header_bytes, hs::resource::header_fields, hs::resource::response_queue_bytes}) LT_CHECK_EQ(budget.in_use(kind), 0u);
    }
LT_END_AUTO_TEST(destruction_cancels_owned_suspended_and_queued_handler_frames)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, streaming_response_attempt_resets_instead_of_emitting_an_empty_success)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned failures = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
        x.start_response(http::status::from_code(200), {});
        std::array<std::byte, 1> bytes{}; auto result = co_await x.writer().write(bytes);
        if (!result.status.ok()) ++failures;
        co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
    auto output = h2test::output(engine); LT_CHECK_EQ(failures, 1u);
    LT_CHECK_EQ(h2test::count_type(output, 3), 1u); LT_CHECK_EQ(h2test::responses(output).size(), 0u);
LT_END_AUTO_TEST(streaming_response_attempt_resets_instead_of_emitting_an_empty_success)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, bodyless_response_length_must_match_zero_bytes)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [](exchange& x) -> task<void> {
        http::fields f;
        if (x.head().head_fields.first("x-invalid")) f.append("Content-Length", "1");
        x.respond(http::status::from_code(200), f); co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    auto invalid = h2test::get(); invalid.push_back({"x-invalid", "yes"});
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, invalid)));
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
    auto output = h2test::output(engine); LT_CHECK_EQ(h2test::count_type(output, 3), 1u);
    auto replies = h2test::responses(output); LT_ASSERT_EQ(replies.size(), 1u); LT_CHECK_EQ(replies[0].stream, 3u);
LT_END_AUTO_TEST(bodyless_response_length_must_match_zero_bytes)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, goaway_reports_the_last_dispatched_stream_and_cancels_parked_handlers)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::resume_signal parked; unsigned after = 0;
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> {
        co_await parked.wait(); ++after; co_return;
    }).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); h2test::output(engine);
    LT_CHECK(!h2test::feed(engine, h2test::frame(9, 4, 3)));
    parked.signal(); executor.run_pending(); LT_CHECK_EQ(after, 0u);
    auto output = h2test::output(engine); LT_ASSERT_EQ(output.size(), 17u);
    LT_CHECK_EQ(output[12], 1u);
    LT_CHECK_EQ(budget.in_use(hs::resource::streams), 0u);
LT_END_AUTO_TEST(goaway_reports_the_last_dispatched_stream_and_cancels_parked_handlers)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, hierarchical_admission_refusal_rolls_back_all_task_resources)
    for (auto kind : {hs::resource::streams, hs::resource::header_fields, hs::resource::header_bytes, hs::resource::body_buffer_bytes}) {
        hs::budget_limits capacities; auto root = hs::resource_budget::root(capacities);
        capacities.set(kind, 0); hs::resource_budget budget; LT_ASSERT(root.child(capacities, budget).ok());
        hs::route_registry routes; LT_ASSERT(hs::route_registry::create(root, routes).ok());
        unsigned calls = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange&) -> task<void> { ++calls; co_return; }).ok());
        httpserver::manual_executor executor;
        {
            hd::http2_request_engine engine(budget, routes, executor);
            hd::hpack_encoder encoder(root); auto wire = h2test::preface();
            h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, h2test::get())));
            h2test::feed(engine, wire); executor.run_pending(); LT_CHECK_EQ(calls, 0u);
            auto output = h2test::output(engine);
            LT_CHECK_EQ(h2test::count_type(output, kind == hs::resource::streams ? 3 : 7), 1u);
        }
        for (auto resource : {hs::resource::streams, hs::resource::header_fields, hs::resource::header_bytes, hs::resource::body_buffer_bytes, hs::resource::response_queue_bytes}) {
            LT_CHECK_EQ(root.in_use(resource), 0u); LT_CHECK_EQ(budget.in_use(resource), 0u);
        }
    }
LT_END_AUTO_TEST(hierarchical_admission_refusal_rolls_back_all_task_resources)
LT_BEGIN_AUTO_TEST(http2_exchange_suite, head_and_304_lengths_are_metadata_but_conflicting_lengths_reset)
    auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    auto handler = [](exchange& x) -> task<void> {
        http::fields f; f.append("Content-Length", "17");
        if (x.head().head_fields.first("x-conflict")) f.append("content-length", "18");
        x.respond(http::status::from_code(x.head().head_fields.first("x-304") ? 304 : 200), f); co_return;
    };
    LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", handler).ok());
    LT_ASSERT(routes.route(http::method::known(http::method_id::head), "/hello", handler).ok());
    httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
    hd::hpack_encoder encoder(budget); auto wire = h2test::preface();
    auto fields = h2test::get(); fields[0].value = "HEAD";
    h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, fields)));
    fields.push_back({"x-conflict", "yes"});
    h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, fields)));
    fields = h2test::get(); fields.push_back({"x-304", "yes"});
    h2test::append(wire, h2test::frame(1, 5, 5, h2test::encode(encoder, fields)));
    LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
    auto output = h2test::output(engine); LT_CHECK_EQ(h2test::count_type(output, 3), 1u);
    auto replies = h2test::responses(output); LT_ASSERT_EQ(replies.size(), 2u);
    LT_CHECK_EQ(replies[0].stream, 1u); LT_CHECK_EQ(replies[0].fields[0].value, "200");
    LT_CHECK_EQ(replies[1].stream, 5u); LT_CHECK_EQ(replies[1].fields[0].value, "304");
LT_END_AUTO_TEST(head_and_304_lengths_are_metadata_but_conflicting_lengths_reset)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
