/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <httpserver/exchange.hpp>
#include "../conformance/corpus.hpp"
#include "./http2_request_fixture.hpp"
#include "./littletest.hpp"
namespace {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
struct observation {
    std::size_t consumed = 0;
    std::optional<hd::http2_error> terminal;
    std::vector<std::uint8_t> output;
    std::vector<unsigned> routes;
};
observation replay(const protocol_corpus::entry& c, std::size_t split, bool bytewise, bool partial) {
    observation seen;
    auto budget = h2test::budget();
    {
        hs::route_registry routes;
        if (!hs::route_registry::create(budget, routes).ok()) throw std::runtime_error("route registry");
        routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                seen.routes.push_back(1);
                x.respond(httpserver::http::status::from_code(204), {}); co_return;
            });
        routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/sibling",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                seen.routes.push_back(3); x.respond(httpserver::http::status::from_code(204), {}); co_return;
            });
        httpserver::manual_executor executor;
        hd::http2_request_engine engine(budget, routes, executor);
        auto drain = [&] { h2test::append(seen.output, h2test::output(engine, partial ? 3 : 65536)); };
        if (c.stage != "unadvertised") drain();
        while (seen.consumed < c.wire.size() && !engine.failure()) {
            auto end = bytewise ? seen.consumed + 1 : c.wire.size();
            if (seen.consumed < split) end = std::min(end, split);
            auto wire = std::span(reinterpret_cast<const std::uint8_t*>(c.wire.data()), end).subspan(seen.consumed);
            engine.begin_turn();
            auto result = engine.feed(wire);
            protocol_corpus::require(result.consumed <= wire.size(), c, "overconsumption");
            if (!result.consumed && result.progress != hd::http2_progress::yield) throw std::runtime_error("parser parked");
            seen.consumed += result.consumed;
            if (c.stage == "pump" && (result.progress == hd::http2_progress::frame_ready || result.error)) {
                executor.run_pending(); drain();
            }
        }
        if (c.stage == "eof") engine.eof();
        executor.run_pending(); drain(); executor.run_pending(); drain();
        seen.terminal = engine.failure();
        if (seen.terminal) {
            auto result = engine.feed(h2test::preface());
            protocol_corpus::require(!result.consumed && result.error.has_value(), c, "terminal not sticky");
            protocol_corpus::require(engine.output().empty(), c, "duplicate GOAWAY");
        }
    }
    for (std::size_t i = 0; i < hs::resource_count; ++i)
        protocol_corpus::require(budget.in_use(static_cast<hs::resource>(i)) == 0, c, "reservation leak");
    return seen;
}
void verify(const protocol_corpus::entry& c, const observation& seen) {
    std::string scope; unsigned code, stream;
    std::istringstream verdict(c.verdict); verdict >> scope >> code >> stream;
    auto frames = h2test::frames(seen.output);
    std::vector<h2test::stream_reset> resets = h2test::resets(seen.output);
    if (scope == "connection") {
        protocol_corpus::require(seen.terminal && static_cast<unsigned>(seen.terminal->wire_code) == code, c, "connection code");
        protocol_corpus::require(!frames.empty() && frames.back().type == 7, c, "GOAWAY ordering");
        protocol_corpus::require(h2test::read_u32(frames.back().payload.data() + 4) == code, c, "GOAWAY code");
    } else {
        protocol_corpus::require(!seen.terminal, c, "unexpected connection failure");
        if (scope == "stream") {
            protocol_corpus::require(resets.size() == 1 && resets[0].stream == stream && resets[0].code == code, c, "reset scope/code/count");
        } else {
            protocol_corpus::require(resets.empty(), c, "unexpected reset");
        }
        protocol_corpus::require(seen.consumed == c.wire.size(), c, "suffix not consumed");
    }
    std::vector<unsigned> expected;
    std::istringstream ids(c.payload); unsigned id;
    while (ids >> id) expected.push_back(id);
    protocol_corpus::require(seen.routes == expected, c, "route effects");
}
}  // namespace
LT_BEGIN_SUITE(http2_engine_conformance_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_engine_conformance_suite)
LT_BEGIN_AUTO_TEST(http2_engine_conformance_suite, rfc9113_engine_transcripts)
    const auto corpus = protocol_corpus::load(HTTP2_ENGINE_CORPUS_DIR);
    std::size_t checked = 0;
    for (const auto& c : corpus) {
        try {
        auto first = replay(c, 0, false, false); verify(c, first);
        auto check = [&](const observation& next) {
            verify(c, next);
            protocol_corpus::require(first.output == next.output && first.consumed == next.consumed, c, "fragmentation changed outcome");
        };
        check(replay(c, 0, true, true));
        for (std::size_t split = 0; split <= c.wire.size(); ++split) check(replay(c, split, false, true));
        ++checked;
        } catch (const std::exception& error) { std::cerr << error.what() << "\n"; LT_CHECK(false); }
    }
    LT_CHECK_EQ(checked, corpus.size());
LT_END_AUTO_TEST(rfc9113_engine_transcripts)
LT_BEGIN_AUTO_TEST(http2_engine_conformance_suite, advertised_stream_admission_limit)
    auto budget = h2test::budget();
    hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    httpserver::manual_executor executor;
    hd::http2_request_limits limits; limits.max_streams = 2;
    hd::http2_request_engine engine(budget, routes, executor, limits);
    auto frames = h2test::frames(h2test::output(engine));
    bool found = false;
    for (const auto& frame : frames) if (frame.type == 4) {
        for (std::size_t at = 0; at + 6 <= frame.payload.size(); at += 6) {
            if (frame.payload[at] == 0 && frame.payload[at + 1] == 3) {
                found = true; LT_CHECK_EQ(h2test::read_u32(frame.payload.data() + at + 2), 2u);
            }
        }
    }
    LT_CHECK(found);
LT_END_AUTO_TEST(advertised_stream_admission_limit)
LT_BEGIN_AUTO_TEST(http2_engine_conformance_suite, local_reset_compression_survives_closed_history_pressure)
    auto budget = h2test::budget(); hs::route_registry routes;
    LT_ASSERT(hs::route_registry::create(budget, routes).ok());
    unsigned routed = 0;
    routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/",
        [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++routed; x.respond(httpserver::http::status::from_code(204), {}); co_return;
        });
    httpserver::manual_executor executor; hd::http2_request_limits limits;
    limits.connection.control_events_per_interval = 4096;
    limits.connection.stream_openings_per_interval = 4096;
    hd::http2_request_engine engine(budget, routes, executor, limits);
    h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::preface()));
    // Invalid opening still inserts :authority in the compression table.
    std::vector<std::uint8_t> rejected{0x82, 0x82, 0x87, 0x84, 0x41, 0x01, 'a'};
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, rejected))); h2test::output(engine);
    for (unsigned id = 3; id < 263; id += 2) {
        LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, id, {0x82, 0x87, 0x84})));
        executor.run_pending(); h2test::output(engine);
    }
    // Delayed HEADERS on our reset stream must still update HPACK.
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 1, {0x40, 0x01, 'x', 0x01, 'y'})));
    LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, 263, {0x82, 0x87, 0x84, 0xbf, 0xbe})));
    executor.run_pending();
    LT_CHECK(!engine.failure()); LT_CHECK_EQ(routed, 131u);
LT_END_AUTO_TEST(local_reset_compression_survives_closed_history_pressure)
LT_BEGIN_AUTO_TEST(http2_engine_conformance_suite, local_reset_history_exhaustion_fails_closed)
    auto budget = h2test::budget();
    unsigned routed = 0;
    {
        hs::route_registry routes; LT_ASSERT(hs::route_registry::create(budget, routes).ok());
        routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                ++routed; x.respond(httpserver::http::status::from_code(204), {}); co_return;
            });
        httpserver::manual_executor executor; hd::http2_request_limits limits;
        limits.connection.control_events_per_interval = 4096;
        limits.connection.stream_openings_per_interval = 4096;
        hd::http2_request_engine engine(budget, routes, executor, limits);
        h2test::output(engine); LT_ASSERT(h2test::feed(engine, h2test::preface()));
        h2test::output(engine);
        // Each duplicate :method opening is locally reset. Retire each reset so
        // output queues and rate limits cannot mask the 128-entry history bound.
        const std::vector<std::uint8_t> malformed{0x82, 0x82, 0x87, 0x84};
        for (unsigned n = 0; n < 128; ++n) {
            const unsigned id = 2 * n + 1;
            LT_ASSERT(h2test::feed(engine, h2test::frame(1, 5, id, malformed)));
            executor.run_pending();
            const auto output = h2test::output(engine);
            const auto resets = h2test::resets(output);
            LT_ASSERT_EQ(resets.size(), 1u);
            LT_CHECK_EQ(resets[0].stream, id);
            LT_CHECK(resets[0].code == static_cast<unsigned>(hd::http2_error_code::protocol_error));
            LT_CHECK_EQ(h2test::count_type(output, 7), 0u);
            LT_CHECK(!engine.failure());
        }

        h2test::feed(engine, h2test::frame(1, 5, 257, malformed));
        const auto failure = engine.failure();
        LT_ASSERT(failure.has_value());
        LT_CHECK(failure->scope == hd::http2_error_scope::connection);
        LT_CHECK(failure->wire_code == hd::http2_error_code::enhance_your_calm);
        LT_CHECK(failure->outcome == httpserver::http::outcome_code::limit_exceeded);
        executor.run_pending();
        const auto terminal = h2test::output(engine);
        const auto frames = h2test::frames(terminal);
        LT_ASSERT_EQ(frames.size(), 1u);
        LT_CHECK_EQ(frames[0].type, 7u);
        LT_CHECK_EQ(frames[0].stream, 0u);
        LT_ASSERT(frames[0].payload.size() >= 8);
        LT_CHECK(h2test::read_u32(frames[0].payload.data() + 4)
                 == static_cast<unsigned>(hd::http2_error_code::enhance_your_calm));

        const auto retry = engine.feed(h2test::frame(1, 5, 259, {0x82, 0x87, 0x84}));
        LT_CHECK_EQ(retry.consumed, 0u);
        LT_ASSERT(retry.error.has_value());
        LT_CHECK(retry.error->wire_code == failure->wire_code);
        LT_CHECK(retry.error->outcome == failure->outcome);
        executor.run_pending();
        LT_CHECK(engine.output().empty());
        LT_CHECK_EQ(routed, 0u);
    }
    for (std::size_t i = 0; i < hs::resource_count; ++i)
        LT_CHECK_EQ(budget.in_use(static_cast<hs::resource>(i)), 0u);
LT_END_AUTO_TEST(local_reset_history_exhaustion_fails_closed)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
