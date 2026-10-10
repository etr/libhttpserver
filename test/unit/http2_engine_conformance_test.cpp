/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <array>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <httpserver/exchange.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
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
    std::vector<httpserver::http::request_head> heads;
    std::vector<std::string> events;
    unsigned admitted = 0, cancelled = 0;
    unsigned destroyed = 0, resumed = 0, cancellations = 0;
    httpserver::http::outcome_code disconnect = httpserver::http::outcome_code::ok;
    httpserver::http::outcome_code cancellation = httpserver::http::outcome_code::ok;
    httpserver::http::outcome_code body_completion = httpserver::http::outcome_code::ok;
};
enum class park_request { no, application, body };
httpserver::task<void> await_cancellation(httpserver::stop_token token) {
    co_await token.cancelled();
}
struct handler_lifetime {
    observation& seen;
    httpserver::exchange& request;
    ~handler_lifetime() {
        ++seen.destroyed;
        seen.events.push_back("destroyed:1");
        seen.disconnect = request.disconnect_reason().code();
    }
};
observation replay(const protocol_corpus::entry& c, std::size_t split, bool bytewise, bool partial,
                   park_request park = park_request::no) {
    // A later stream-error transcript requires the opening exchange to remain
    // active. A completed response would instead close an END_STREAM opening.
    if (park == park_request::no && c.stage == "dispatch") park = park_request::application;
    protocol_corpus::require(c.stage == "feed" || c.stage == "pump" || c.stage == "dispatch"
        || c.stage == "eof" || c.stage == "unadvertised", c, "unknown executor stage");
    observation seen;
    auto budget = h2test::budget();
    {
        hs::route_registry routes;
        if (!hs::route_registry::create(budget, routes).ok()) throw std::runtime_error("route registry");
        httpserver::manual_executor executor;
        httpserver::resume_signal pause;
        routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                seen.routes.push_back(1);
                seen.events.push_back("started:1");
                seen.heads.push_back(x.head());
                if (park != park_request::no) {
                    handler_lifetime lifetime{seen, x};
                    httpserver::spawn(executor, await_cancellation(x.cancellation()), [&](httpserver::task_result<void> result) {
                        ++seen.cancellations;
                        seen.events.push_back("token-completed:1");
                        protocol_corpus::require(result.has_outcome(), c, "untyped cancellation");
                        seen.cancellation = result.outcome();
                    });
                    if (park == park_request::application) {
                        co_await pause.wait();
                    } else {
                        protocol_corpus::require(x.admit_body({4}).ok(), c, "body admission");
                        std::array<std::byte, 4> bytes;
                        const auto read = co_await x.body().read_some(bytes);
                        seen.body_completion = read.status.code();
                        ++seen.resumed;
                        co_return;
                    }
                    ++seen.resumed;
                }
                x.respond(httpserver::http::status::from_code(204), {}); co_return;
            });
        routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/sibling",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                seen.routes.push_back(3); seen.heads.push_back(x.head());
                seen.events.push_back("started:3");
                x.respond(httpserver::http::status::from_code(204), {}); co_return;
            });
        hd::request_dispatch_context dispatch;
        dispatch.diagnostics = hs::diagnostics([&](const hs::diagnostic_event& event) {
            if (event.kind == hs::diagnostic_kind::stream_admitted) {
                if (event.stream_id == 1) ++seen.admitted;
                seen.events.push_back("admitted:" + std::to_string(event.stream_id));
            } else if (event.kind == hs::diagnostic_kind::stream_cancelled && event.stream_id == 1) {
                ++seen.cancelled;
                seen.events.push_back("cancelled:1");
            }
        });
        hd::http2_request_engine engine(budget, routes, executor, {}, dispatch);
        auto drain = [&] { h2test::append(seen.output, h2test::output(engine, partial ? 3 : 65536)); };
        if (c.stage != "unadvertised") drain();
        while (seen.consumed < c.wire.size() && !engine.failure()) {
            auto end = bytewise ? seen.consumed + 1 : c.wire.size();
            if (seen.consumed < split) end = std::min(end, split);
            auto wire = std::span(reinterpret_cast<const std::uint8_t*>(c.wire.data()), end).subspan(seen.consumed);
            engine.begin_turn();
            dispatch.diagnostics.dispatch();
            auto result = engine.feed(wire);
            dispatch.diagnostics.dispatch();
            protocol_corpus::require(result.consumed <= wire.size(), c, "overconsumption");
            if (!result.consumed && result.progress != hd::http2_progress::yield) throw std::runtime_error("parser parked");
            seen.consumed += result.consumed;
            if ((c.stage == "pump" || c.stage == "dispatch")
                    && (result.progress == hd::http2_progress::frame_ready || result.error)) {
                executor.run_pending();
                // Dispatch-only cases leave wire retirement until after input.
                if (c.stage == "pump") drain();
            }
        }
        if (c.stage == "eof") engine.eof();
        executor.run_pending(); drain(); executor.run_pending(); drain();
        pause.signal(); executor.run_pending(); drain();
        dispatch.diagnostics.dispatch();
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
        protocol_corpus::require(h2test::count_type(seen.output, 7) == 1, c, "GOAWAY count");
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
bool later_stream_error(const protocol_corpus::entry& c) {
    return c.name == "content-length-data" || c.name == "malformed-trailer" || c.name == "closed-data"
        || c.name == "stream-window-overflow" || c.name == "half-closed-head";
}
void append_dynamic_sibling(protocol_corpus::entry& c) {
    // An independently authored literal on the reset stream creates index 62;
    // the sibling must decode that index even though the field block is discarded.
    auto suffix = h2test::frame(1, 5, 1, {0x40, 7, 'x', '-', 'r', 'o', 'u', 't', 'e', 5, 'a', 'f', 't', 'e', 'r'});
    h2test::append(suffix, h2test::frame(1, 5, 3, {0x82, 0x87, 0x04, 8, '/', 's', 'i', 'b', 'l', 'i', 'n', 'g', 0xbe}));
    c.wire.append(reinterpret_cast<const char*>(suffix.data()), suffix.size());
}
}  // namespace
LT_BEGIN_SUITE(http2_engine_conformance_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_engine_conformance_suite)
LT_BEGIN_AUTO_TEST(http2_engine_conformance_suite, rfc9113_engine_transcripts)
    const auto corpus = protocol_corpus::load(HTTP2_ENGINE_CORPUS_DIR);
    LT_CHECK_EQ(corpus.size(), 52u);
    std::size_t checked = 0, variants = 0;
    for (const auto& c : corpus) {
        try {
        auto first = replay(c, 0, false, false); verify(c, first); ++variants;
        auto check = [&](const observation& next) {
            verify(c, next);
            ++variants;
            protocol_corpus::require(first.output == next.output && first.consumed == next.consumed, c, "fragmentation changed outcome");
        };
        check(replay(c, 0, true, true));
        for (std::size_t split = 0; split <= c.wire.size(); ++split) check(replay(c, split, false, true));
        ++checked;
        std::cout << "CORPUS name=" << c.name << " stage=" << c.stage << " variants=" << c.wire.size() + 3
                  << " route_checks=" << c.wire.size() + 3 << " routes=" << c.payload << "\n";
        } catch (const std::exception& error) { std::cerr << error.what() << "\n"; LT_CHECK(false); }
    }
    LT_CHECK_EQ(checked, corpus.size());
    std::cout << "CORPUS entries=" << checked << " variants=" << variants << " route_checks=" << variants << "\n";
LT_END_AUTO_TEST(rfc9113_engine_transcripts)
LT_BEGIN_AUTO_TEST(http2_engine_conformance_suite, later_errors_cancel_deferred_or_started_routes_without_losing_hpack)
    unsigned checked = 0, schedules = 0;
    std::size_t variants = 0;
    for (const auto& original : protocol_corpus::load(HTTP2_ENGINE_CORPUS_DIR)) {
        if (!later_stream_error(original)) continue;
        ++checked;
        for (bool start : {false, true}) {
            for (auto park : {park_request::application, park_request::body}) {
                if (park == park_request::body && (original.name == "closed-data" || original.name == "half-closed-head")) continue;
                auto c = original;
                c.stage = start ? "dispatch" : "feed";
                c.payload = start ? "1 3" : "3";
                append_dynamic_sibling(c);
                auto check = [&](const observation& seen) {
                    verify(c, seen);
                    ++variants;
                    protocol_corpus::require(seen.admitted == 1 && seen.cancelled == 1, c, "opening admission/cancellation count");
                    const bool body_completed = start && park == park_request::body;
                    protocol_corpus::require(seen.resumed == (body_completed ? 1u : 0u), c, "cancelled handler resume count");
                    if (body_completed)
                        protocol_corpus::require(seen.body_completion == httpserver::http::outcome_code::connection_closed, c, "body terminal outcome");
                    protocol_corpus::require(seen.destroyed == (start ? 1u : 0u), c, "handler destruction count");
                    protocol_corpus::require(seen.cancellations == (start ? 1u : 0u), c, "cancellation completion count");
                    if (start) {
                        protocol_corpus::require(seen.disconnect == httpserver::http::outcome_code::connection_closed, c, "disconnect outcome");
                        protocol_corpus::require(seen.cancellation == httpserver::http::outcome_code::cancelled, c, "cancellation outcome");
                        protocol_corpus::require(seen.heads.front().raw_target == "/"
                            && seen.heads.front().request_method.name() == "GET"
                            && seen.heads.front().head_fields.first("host") == "example.test", c, "opening head");
                        const auto cancelled = std::find(seen.events.begin(), seen.events.end(), "cancelled:1");
                        const auto started = std::find(seen.events.begin(), seen.events.end(), "started:1");
                        const auto destroyed = std::find(seen.events.begin(), seen.events.end(), "destroyed:1");
                        protocol_corpus::require(started < cancelled && cancelled < destroyed, c, "handler event order");
                        if (c.name == "content-length-data")
                            protocol_corpus::require(seen.heads.front().head_fields.first("content-length") == "2", c, "opening length");
                    }
                    protocol_corpus::require(seen.heads.back().raw_target == "/sibling"
                        && seen.heads.back().head_fields.first("x-route") == "after", c, "dynamic sibling head");
                    const auto frames = h2test::frames(seen.output);
                    const auto reset = std::find_if(frames.begin(), frames.end(), [](const auto& frame) { return frame.type == 3; });
                    const auto response = std::find_if(frames.begin(), frames.end(), [](const auto& frame) { return frame.type == 1 && frame.stream == 3; });
                    protocol_corpus::require(reset < response && response != frames.end(), c, "reset before sibling response");
                    protocol_corpus::require(h2test::count_type(seen.output, 7) == 0, c, "unexpected GOAWAY");
                };
                try {
                    const auto first = replay(c, 0, false, false, park); check(first);
                    std::cout << "SCHEDULE name=" << c.name << " start=" << start << " body=" << (park == park_request::body)
                              << " routes=" << c.payload << " verdict=" << c.verdict << " consumed=" << first.consumed
                              << " destroyed=" << first.destroyed << " body_completions=" << first.resumed
                              << " token_completions=" << first.cancellations << " dynamic=after events=";
                    for (const auto& event : first.events) std::cout << event << ",";
                    std::cout << "\n";
                    auto fragmented = [&](const observation& next) {
                        check(next);
                        protocol_corpus::require(next.output == first.output && next.consumed == first.consumed, c, "schedule fragmentation outcome");
                    };
                    fragmented(replay(c, 0, true, true, park));
                    for (std::size_t split = 0; split <= c.wire.size(); ++split) fragmented(replay(c, split, false, true, park));
                    ++schedules;
                } catch (const std::exception& error) { std::cerr << error.what() << "\n"; LT_CHECK(false); }
            }
        }
    }
    LT_CHECK_EQ(checked, 5u);
    LT_CHECK_EQ(schedules, 16u);
    std::cout << "SCHEDULE transcripts=" << checked << " schedules=" << schedules << " variants=" << variants << " route_checks=" << variants << "\n";
LT_END_AUTO_TEST(later_errors_cancel_deferred_or_started_routes_without_losing_hpack)
LT_BEGIN_AUTO_TEST(http2_engine_conformance_suite, completed_response_before_late_input_has_connection_scope)
    unsigned checked = 0;
    for (auto c : protocol_corpus::load(HTTP2_ENGINE_CORPUS_DIR)) {
        if (c.name != "closed-data" && c.name != "half-closed-head") continue;
        c.stage = "pump";
        c.verdict = "connection 5 0";
        try {
            const auto first = replay(c, 0, false, false); verify(c, first);
            auto check = [&](const observation& next) {
                verify(c, next);
                protocol_corpus::require(next.output == first.output && next.consumed == first.consumed, c, "completed fragmentation outcome");
                protocol_corpus::require(h2test::resets(next.output).empty(), c, "completed stream reset");
            };
            check(first); check(replay(c, 0, true, true));
            for (std::size_t split = 0; split <= c.wire.size(); ++split) check(replay(c, split, false, true));
            ++checked;
        } catch (const std::exception& error) { std::cerr << error.what() << "\n"; LT_CHECK(false); }
    }
    LT_CHECK_EQ(checked, 2u);
LT_END_AUTO_TEST(completed_response_before_late_input_has_connection_scope)
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
