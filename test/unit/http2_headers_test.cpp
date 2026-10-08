/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/exchange.hpp>
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using httpserver::task;
using httpserver::exchange;
LT_BEGIN_SUITE(http2_headers_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_headers_suite)
LT_BEGIN_AUTO_TEST(http2_headers_suite, padded_priority_continuations_at_every_input_split)
    auto fields = h2test::get();
    fields.push_back({"x-repeat", "first"}); fields.push_back({"x-repeat", "second"});
    hd::hpack_encoder encoder(h2test::budget());
    auto block = h2test::encode(encoder, fields);
    auto wire = h2test::preface();
    std::vector<std::uint8_t> start{2, 0, 0, 0, 0, 20};
    start.insert(start.end(), block.begin(), block.begin() + 2);
    start.insert(start.end(), {0, 0});
    h2test::append(wire, h2test::frame(1, 0x29, 1, start));
    h2test::append(wire, h2test::frame(9, 0, 1, {block.begin() + 2, block.begin() + 4}));
    h2test::append(wire, h2test::frame(9, 4, 1, {block.begin() + 4, block.end()}));
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        auto budget = h2test::budget(); hs::route_registry routes;
        LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        unsigned calls = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            ++calls;
            LT_CHECK_EQ(x.head().raw_target, "/items/../hello?x=1");
            LT_CHECK(x.head().request_protocol == http::protocol::http_2);
            LT_CHECK_EQ(x.head().head_fields.size(), 3u);
            LT_CHECK_EQ(x.head().head_fields.entries().back().name, "host");
            LT_CHECK_EQ(x.head().head_fields.all("x-repeat")[0], "first");
            LT_CHECK_EQ(x.head().head_fields.all("x-repeat")[1], "second");
            x.respond(http::status::from_code(204), {}); co_return;
        }).ok());
        httpserver::manual_executor executor;
        hd::http2_request_engine engine(budget, routes, executor);
        LT_ASSERT(h2test::feed(engine, std::span(wire).first(split)));
        LT_ASSERT(h2test::feed(engine, std::span(wire).subspan(split)));
        executor.run_pending();
        LT_CHECK_EQ(calls, 1u);
        auto responses = h2test::responses(h2test::output(engine, 3));
        LT_ASSERT_EQ(responses.size(), 1u);
        LT_CHECK_EQ(responses[0].stream, 1u);
        LT_CHECK_EQ(responses[0].fields[0].value, "204");
    }
LT_END_AUTO_TEST(padded_priority_continuations_at_every_input_split)
LT_BEGIN_AUTO_TEST(http2_headers_suite, semantic_errors_reset_only_the_stream_and_preserve_compression)
    std::vector<std::vector<hd::hpack_field>> invalid;
    auto add = [&](std::vector<hd::hpack_field> f) { invalid.push_back(std::move(f)); };
    auto f = h2test::get(); f.push_back({":path", "/hello"}); add(f);
    f = h2test::get(); f.erase(f.begin()); add(f);
    f = h2test::get(); f.erase(f.begin() + 1); add(f);
    f = h2test::get(); f.erase(f.begin() + 2); add(f);
    f = h2test::get(); f.insert(f.begin(), {"x-early", "yes"}); add(f);
    for (auto name : {":status", ":unknown", "Upper", "connection", "proxy-connection", "keep-alive", "upgrade", "transfer-encoding", "te", "content-length", "host"}) {
        f = h2test::get(); f.push_back({name, "bad"}); add(f);
    }
    f = h2test::get("http://example.test/hello"); add(f);
    f = h2test::get("/hello#fragment"); add(f);
    for (auto scheme : {"1bad", "a:b", ""}) {
        f = h2test::get(); f[1].value = scheme; add(f);
    }
    f = h2test::get(); f.push_back({"x-value", " bad"}); add(f);
    f = h2test::get(); f.push_back({"x-value", std::string("bad\0value", 9)}); add(f);
    for (auto bad : invalid) {
        auto budget = h2test::budget(); hs::route_registry routes;
        LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        unsigned calls = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            ++calls; LT_CHECK_EQ(x.head().head_fields.first("x-dynamic").value_or(""), "inserted");
            x.respond(http::status::from_code(200), {}); co_return;
        }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
        hd::hpack_encoder encoder(budget);
        bad.push_back({"x-dynamic", "inserted"});
        auto good = h2test::get(); good.push_back({"x-dynamic", "inserted"});
        auto wire = h2test::preface();
        h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, bad)));
        h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, good)));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
        LT_CHECK(!engine.failure()); LT_CHECK_EQ(calls, 1u);
        auto output = h2test::output(engine);
        auto resets = h2test::resets(output); LT_ASSERT_EQ(resets.size(), 1u);
        LT_CHECK_EQ(resets[0].stream, 1u); LT_CHECK_EQ(resets[0].code, 1u);
        auto responses = h2test::responses(output);
        LT_ASSERT_EQ(responses.size(), 1u); LT_CHECK_EQ(responses[0].stream, 3u);
    }
LT_END_AUTO_TEST(semantic_errors_reset_only_the_stream_and_preserve_compression)
LT_BEGIN_AUTO_TEST(http2_headers_suite, continuation_and_compression_errors_are_terminal)
    const std::vector<std::vector<std::uint8_t>> endings{
        h2test::frame(9, 4, 1),
        h2test::frame(1, 1, 1, {0x82}),
        [] { auto w = h2test::frame(1, 1, 1, {0x82}); h2test::append(w, h2test::frame(9, 4, 3)); return w; }(),
        [] { auto w = h2test::frame(1, 1, 1, {0x82}); h2test::append(w, h2test::frame(6, 0, 0, std::vector<std::uint8_t>(8))); return w; }(),
        h2test::frame(1, 5, 1, {0xff})};
    for (std::size_t i = 0; i < endings.size(); ++i) {
        auto budget = h2test::budget(); hs::route_registry routes;
        LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
        auto wire = h2test::preface(); h2test::append(wire, endings[i]);
        h2test::feed(engine, wire); engine.eof();
        LT_ASSERT(engine.failure());
        LT_CHECK(engine.failure()->scope == hd::http2_error_scope::connection);
        LT_CHECK_EQ(static_cast<unsigned>(engine.failure()->wire_code), i == 4 ? 9u : 1u);
        LT_CHECK_EQ(h2test::count_type(h2test::output(engine), 7), 1u);
    }
LT_END_AUTO_TEST(continuation_and_compression_errors_are_terminal)
LT_BEGIN_AUTO_TEST(http2_headers_suite, deprecated_priority_still_decodes_the_complete_block_in_wire_order)
    for (bool continuation : {false, true}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        unsigned calls = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            ++calls; LT_CHECK_EQ(x.head().head_fields.first("x-inserted").value_or(""), "priority-ignored");
            x.respond(http::status::from_code(204), {}); co_return;
        }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
        hd::hpack_encoder encoder(budget); auto fields = h2test::get(); fields.push_back({"x-inserted", "priority-ignored"});
        auto block = h2test::encode(encoder, fields); std::vector<std::uint8_t> priority{0, 0, 0, 1, 20};
        priority.insert(priority.end(), block.begin(), continuation ? block.begin() + 2 : block.end());
        auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, continuation ? 33 : 37, 1, priority));
        if (continuation) h2test::append(wire, h2test::frame(9, 4, 1, {block.begin() + 2, block.end()}));
        h2test::append(wire, h2test::frame(1, 5, 3, h2test::encode(encoder, fields)));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending();
        LT_CHECK_EQ(calls, 2u); LT_CHECK(!engine.failure());
        auto output = h2test::output(engine); auto resets = h2test::resets(output); LT_CHECK(resets.empty());
        auto replies = h2test::responses(output); LT_ASSERT_EQ(replies.size(), 2u); LT_CHECK_EQ(replies[0].stream, 1u); LT_CHECK_EQ(replies[1].stream, 3u);
    }
LT_END_AUTO_TEST(deprecated_priority_still_decodes_the_complete_block_in_wire_order)
LT_BEGIN_AUTO_TEST(http2_headers_suite, valid_uri_schemes_and_empty_body_fields_reach_the_route)
    for (const auto& scheme : {"https", "HTTPS", "web+demo", "urn"}) {
        auto budget = h2test::budget(); hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        unsigned calls = 0;
        LT_ASSERT(routes.route(http::method::known(http::method_id::get), "/hello", [&](exchange& x) -> task<void> {
            ++calls; LT_CHECK_EQ(x.head().head_fields.count("content-length"), 2u);
            x.respond(http::status::from_code(204), {}); co_return;
        }).ok());
        httpserver::manual_executor executor; hd::http2_request_engine engine(budget, routes, executor);
        hd::hpack_encoder encoder(budget); auto fields = h2test::get(); fields[1].value = scheme;
        fields.push_back({"host", "EXAMPLE.test"}); fields.push_back({"te", "trailers"});
        fields.push_back({"content-length", "0"}); fields.push_back({"content-length", "00"});
        auto wire = h2test::preface(); h2test::append(wire, h2test::frame(1, 5, 1, h2test::encode(encoder, fields)));
        LT_ASSERT(h2test::feed(engine, wire)); executor.run_pending(); LT_CHECK_EQ(calls, 1u);
        LT_CHECK_EQ(h2test::responses(h2test::output(engine)).size(), 1u);
    }
LT_END_AUTO_TEST(valid_uri_schemes_and_empty_body_fields_reach_the_route)
LT_BEGIN_AUTO_TEST(http2_headers_suite, response_oracle_rejects_truncated_and_unfinished_blocks)
    auto valid = h2test::frame(1, 5, 1, {0x89});
    LT_ASSERT_EQ(h2test::responses(valid).size(), 1u);
    std::vector<std::vector<std::uint8_t>> malformed;
    auto wire = valid; wire.push_back(0xff); malformed.push_back(wire);
    wire = valid; h2test::append(wire, h2test::frame(1, 1, 3, {0x89})); malformed.push_back(wire);
    wire = h2test::frame(1, 1, 1, {0x89}); h2test::append(wire, h2test::frame(1, 5, 3, {0x89})); malformed.push_back(wire);
    malformed.push_back(h2test::frame(9, 4, 0, {0x89}));
    malformed.push_back(h2test::frame(9, 4, 1, {0x89}));
    for (const auto& bad : malformed) {
        bool rejected = false;
        try {
            h2test::responses(bad);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        LT_CHECK(rejected);
    }
LT_END_AUTO_TEST(response_oracle_rejects_truncated_and_unfinished_blocks)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
