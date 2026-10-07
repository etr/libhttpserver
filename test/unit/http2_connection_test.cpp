/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <vector>
#include <httpserver/detail/http2_connection.hpp>
#include "./http2_fixture.hpp"
#include "./littletest.hpp"
using namespace h2test;  // NOLINT(build/namespaces)
namespace {
void open(hd::http2_connection& c) {
    c.advance_output(c.output().size());
    c.feed(preface());
    c.advance_output(c.output().size());
    c.feed(frame(4, 1));
}
}
LT_BEGIN_SUITE(http2_connection_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_connection_suite)
LT_BEGIN_AUTO_TEST(http2_connection_suite, data_header_fragment_reserves_once_and_releases_event)
    hs::budget_limits limits;
    limits.set(hs::resource::body_buffer_bytes, 4);
    auto b = hs::resource_budget::root(limits);
    hd::http2_connection c(b);
    open(c);
    auto data = frame(0, 255, 0x80000001, {0, 2, 3, 4});
    LT_CHECK_EQ(c.feed(std::span(data).first(9)).consumed, 9U);
    LT_CHECK_EQ(b.in_use(hs::resource::body_buffer_bytes), 4U);
    auto result = c.feed(std::span(data).subspan(9));
    LT_ASSERT(!result.error);
    LT_CHECK_EQ(result.consumed, 4U);
    LT_CHECK_EQ(c.header().stream_id, 1U);
    LT_CHECK_EQ(c.payload().size(), 4U);
    LT_CHECK_EQ(c.feed(frame(4)).consumed, 0U);
    c.release_frame();
    LT_CHECK_EQ(b.in_use(hs::resource::body_buffer_bytes), 0U);
LT_END_AUTO_TEST(data_header_fragment_reserves_once_and_releases_event)
LT_BEGIN_AUTO_TEST(http2_connection_suite, ping_binary_echo_and_ack_has_no_response)
    for (std::size_t split = 0; split <= 17; ++split) {
        hd::http2_connection c(budget());
        open(c);
        auto ping = frame(6, 254, 0, {0, 255, 128, 1, 0, 19, 244, 2});
        auto a = c.feed(std::span(ping).first(split));
        auto result = split == 17 ? a : c.feed(std::span(ping).subspan(split));
        LT_ASSERT(!result.error);
        auto expected = ping;
        expected[4] = 1;
        auto out = c.output();
        LT_CHECK(std::equal(out.begin(), out.end(), expected.begin(), expected.end()));
        LT_CHECK(!c.advance_output(18));
        c.advance_output(4);
        LT_CHECK_EQ(c.output().size(), 13U);
        c.advance_output(13);
        ping[4] = 1;
        LT_CHECK(c.feed(ping).progress == hd::http2_progress::control_ready);
        LT_CHECK(c.output().empty());
    }
LT_END_AUTO_TEST(ping_binary_echo_and_ack_has_no_response)
LT_BEGIN_AUTO_TEST(http2_connection_suite, frame_errors_have_defined_scope_and_resynchronize_stream_errors)
    struct example { std::vector<std::uint8_t> wire; hd::http2_error_code code; bool stream; };
    for (const auto& item : std::vector<example>{
        {frame(6, 0, 0, {1}), hd::http2_error_code::frame_size_error, false},
        {frame(6, 0, 1, std::vector<std::uint8_t>(8)), hd::http2_error_code::protocol_error, false},
        {frame(2, 0, 1, {1}), hd::http2_error_code::frame_size_error, true},
        {frame(8, 0, 1, {0, 0, 0, 0}), hd::http2_error_code::protocol_error, true},
        {frame(8, 0, 0, {128, 0, 0, 0}), hd::http2_error_code::protocol_error, false},
        {frame(5, 4, 1, {0, 0, 0, 2}), hd::http2_error_code::protocol_error, false},
        {frame(1, 8, 1, {1}), hd::http2_error_code::protocol_error, false},
        {frame(3, 0, 1, {0}), hd::http2_error_code::frame_size_error, false},
        {frame(2, 0, 1, {0, 0, 0, 1, 5}), hd::http2_error_code::protocol_error, true}}) {
        hd::http2_connection c(budget());
        open(c);
        auto result = c.feed(item.wire);
        LT_ASSERT(result.error.has_value());
        LT_CHECK(result.error->wire_code == item.code);
        LT_CHECK((result.error->scope == hd::http2_error_scope::stream) == item.stream);
        if (item.stream) {
            LT_CHECK_EQ(result.consumed, item.wire.size());
            c.release_frame();
            LT_CHECK(c.feed(frame(4)).progress == hd::http2_progress::control_ready);
        } else {
            LT_CHECK_EQ(c.feed(preface()).consumed, 0U);
            auto out = c.output();
            LT_ASSERT(out.size() == 17);
            LT_CHECK_EQ(out[3], 7);
            c.advance_output(17);
            LT_CHECK(c.output().empty());
        }
    }
LT_END_AUTO_TEST(frame_errors_have_defined_scope_and_resynchronize_stream_errors)
LT_BEGIN_AUTO_TEST(http2_connection_suite, continuation_order_and_eof)
    for (const auto& next : {frame(9, 4, 3), frame(6, 0, 0, std::vector<std::uint8_t>(8)), frame(99)}) {
        hd::http2_connection c(budget());
        open(c);
        LT_CHECK(c.feed(frame(1, 0, 1)).progress == hd::http2_progress::frame_ready);
        c.release_frame();
        LT_CHECK(c.feed(next).error.has_value());
    }
    hd::http2_connection c(budget());
    open(c);
    c.feed(frame(1, 0, 1));
    c.release_frame();
    LT_CHECK(c.eof().error.has_value());
    hd::http2_connection valid(budget());
    open(valid);
    valid.feed(frame(1, 0, 1));
    valid.release_frame();
    LT_CHECK(!valid.feed(frame(9, 4, 1)).error);
    valid.release_frame();
    LT_CHECK(!valid.eof().error);
    LT_CHECK(valid.feed(frame(9, 4, 1)).error.has_value());
LT_END_AUTO_TEST(continuation_order_and_eof)
LT_BEGIN_AUTO_TEST(http2_connection_suite, control_reserve_survives_data_pressure_and_ancestor_refusal_rolls_back)
    hs::budget_limits limits;
    limits.set(hs::resource::response_queue_bytes, 4096);
    auto parent = hs::resource_budget::root(limits);
    hs::resource_budget child;
    LT_ASSERT(parent.child(limits, child).ok());
    {
        hd::http2_connection c(child);
        open(c);
        hs::reservation data;
        LT_ASSERT(child.reserve(hs::resource::body_buffer_bytes, child.capacity(hs::resource::body_buffer_bytes), data).ok());
        LT_CHECK(!c.feed(frame(4)).error);
        LT_CHECK(!c.feed(frame(6, 0, 0, std::vector<std::uint8_t>(8))).error);
        hd::http2_connection refused(child);
        LT_ASSERT(refused.failure().has_value());
        LT_CHECK(refused.failure()->outcome == httpserver::http::outcome_code::limit_exceeded);
        LT_CHECK_EQ(parent.in_use(hs::resource::response_queue_bytes), 4096U);
        LT_CHECK_EQ(child.in_use(hs::resource::response_queue_bytes), 4096U);
        LT_CHECK(refused.output().empty());
        LT_CHECK(c.feed(frame(0, 0, 1, {1})).error.has_value());
        LT_CHECK_EQ(child.in_use(hs::resource::body_buffer_bytes), data.units());
        c.advance_output(c.output().size());
        LT_CHECK_EQ(child.in_use(hs::resource::response_queue_bytes), 0U);
    }
    LT_CHECK_EQ(parent.in_use(hs::resource::response_queue_bytes), 0U);
    LT_CHECK_EQ(parent.in_use(hs::resource::body_buffer_bytes), 0U);
LT_END_AUTO_TEST(control_reserve_survives_data_pressure_and_ancestor_refusal_rolls_back)
LT_BEGIN_AUTO_TEST(http2_connection_suite, queue_and_pending_limits_and_work_yield)
    hd::http2_limits limits;
    limits.control_frames = 2;
    limits.pending_settings = 2;
    limits.frames_per_turn = 3;
    hd::http2_connection c(budget(), limits);
    open(c);
    c.begin_turn();
    auto ping = frame(6, 0, 0, std::vector<std::uint8_t>(8));
    LT_CHECK(!c.feed(ping).error);
    LT_CHECK(!c.feed(ping).error);
    auto full = c.feed(ping);
    LT_ASSERT(full.error.has_value());
    LT_CHECK(full.error->wire_code == hd::http2_error_code::enhance_your_calm);
    hd::http2_connection yielding(budget(), limits);
    open(yielding);
    yielding.feed(frame(99));
    yielding.release_frame();
    LT_CHECK(yielding.feed(frame(4)).progress == hd::http2_progress::yield);
    yielding.begin_turn();
    LT_CHECK(!yielding.feed(frame(4)).error);
    yielding.advance_output(yielding.output().size());
    LT_CHECK(yielding.queue_settings({}) == httpserver::http::outcome_code::ok);
    yielding.advance_output(yielding.output().size());
    LT_CHECK(yielding.queue_settings({}) == httpserver::http::outcome_code::ok);
    LT_CHECK(yielding.queue_settings({}) == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK(!yielding.failure());
    for (const auto bad : {hd::http2_limits{0, 4096, 16, 64}, hd::http2_limits{65, 4096, 16, 64}, hd::http2_limits{64, 61, 16, 64}}) {
        hd::http2_connection invalid(budget(), bad);
        LT_ASSERT(invalid.failure().has_value());
        LT_CHECK(invalid.failure()->outcome == httpserver::http::outcome_code::invalid_argument);
    }
LT_END_AUTO_TEST(queue_and_pending_limits_and_work_yield)
LT_BEGIN_AUTO_TEST(http2_connection_suite, malformed_stream_frames_obey_turn_limit_and_header_continuation)
    hd::http2_limits limits;
    limits.frames_per_turn = 2;
    hd::http2_connection c(budget(), limits);
    open(c);
    c.begin_turn();
    for (unsigned i = 0; i < 2; ++i) {
        LT_CHECK(c.feed(frame(2, 0, 1, {1})).error.has_value());
        c.release_frame();
    }
    LT_CHECK(c.feed(frame(2, 0, 1, {1})).progress == hd::http2_progress::yield);
    hd::http2_connection headers(budget());
    open(headers);
    LT_CHECK(headers.feed(frame(1, 32, 1, {0, 0, 0, 1, 0})).error.has_value());
    headers.release_frame();
    LT_CHECK(headers.feed(frame(6, 0, 0, std::vector<std::uint8_t>(8))).error.has_value());
LT_END_AUTO_TEST(malformed_stream_frames_obey_turn_limit_and_header_continuation)
LT_BEGIN_AUTO_TEST(http2_connection_suite, bytes_capacity_and_coalesced_flood_boundaries)
    for (bool bytewise : {false, true}) {
        hd::http2_limits limits;
        limits.control_bytes = 62;
        auto b = budget();
        hd::http2_connection c(b, limits);
        open(c);
        auto wire = frame(6, 0, 0, std::vector<std::uint8_t>(8));
        auto second = wire;
        append(wire, second);
        append(wire, frame(6, 0, 0, std::vector<std::uint8_t>(8)));
        std::size_t offset = 0;
        std::optional<hd::http2_error> error;
        while (offset < wire.size() && !error) {
            const auto n = bytewise ? 1 : wire.size() - offset;
            auto result = c.feed(std::span(wire).subspan(offset, n));
            offset += result.consumed;
            error = result.error;
        }
        LT_CHECK_EQ(offset, 51U);
        LT_ASSERT(error.has_value());
        LT_CHECK(error->wire_code == hd::http2_error_code::enhance_your_calm);
        LT_CHECK_EQ(c.output().size(), 17U);
        c.advance_output(17);
        LT_CHECK_EQ(b.in_use(hs::resource::response_queue_bytes), 0U);
    }
LT_END_AUTO_TEST(bytes_capacity_and_coalesced_flood_boundaries)
LT_BEGIN_AUTO_TEST(http2_connection_suite, terminal_error_finishes_started_control_before_goaway)
    auto b = budget();
    hd::http2_connection c(b);
    open(c);
    auto ping = frame(6, 0, 0, {0, 1, 2, 3, 4, 5, 6, 7});
    c.feed(ping);
    auto first = c.output();
    std::vector<std::uint8_t> expected(first.begin(), first.end());
    c.advance_output(3);
    LT_CHECK(c.feed(frame(9, 4, 1)).error.has_value());
    auto remainder = c.output();
    LT_CHECK(std::equal(remainder.begin(), remainder.end(), expected.begin() + 3, expected.end()));
    LT_CHECK_EQ(b.in_use(hs::resource::response_queue_bytes), 4096U);
    c.advance_output(remainder.size());
    LT_ASSERT(c.output().size() == 17);
    LT_CHECK_EQ(c.output()[3], 7);
    c.advance_output(17);
    LT_CHECK(c.output().empty());
    LT_CHECK_EQ(b.in_use(hs::resource::response_queue_bytes), 0U);
LT_END_AUTO_TEST(terminal_error_finishes_started_control_before_goaway)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
