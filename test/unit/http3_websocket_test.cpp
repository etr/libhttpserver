/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string>
#include "./http3_websocket_fixture.hpp"
#include "./littletest.hpp"
using namespace h3ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h3_ws_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h3_ws_suite)
LT_BEGIN_AUTO_TEST(h3_ws_suite, masked_message_survives_every_wire_split)
    auto wire = h3test::frame(0, masked(1, "hello"));
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request());
        LT_ASSERT(f.feed(0, std::span(wire).first(split)));
        LT_ASSERT(f.feed(0, std::span(wire).subspan(split)));
        auto received = f.take(); LT_ASSERT(received->has_value());
        LT_CHECK_EQ(text(**received), "hello");
        LT_CHECK((*received)->value->kind == h::websocket::message_kind::text);
        LT_ASSERT(f.request(4, h3test::get(), true));
        LT_CHECK_EQ(f.reply(4).heads[0][0].value, "204");
    }
LT_END_AUTO_TEST(masked_message_survives_every_wire_split)
LT_BEGIN_AUTO_TEST(h3_ws_suite, websocket_frame_spans_data_and_ignored_frames)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request());
    auto wire = masked(2, "binary");
    LT_ASSERT(f.data(std::span(wire).first(3))); LT_ASSERT(f.data({}));
    LT_ASSERT(f.feed(0, h3test::frame(0x21, h3test::bytes({255, 255}))));
    LT_ASSERT(f.data(std::span(wire).subspan(3)));
    auto received = f.take(); LT_ASSERT(received->has_value()); LT_CHECK_EQ(text(**received), "binary");
    LT_CHECK((*received)->value->kind == h::websocket::message_kind::binary);
LT_END_AUTO_TEST(websocket_frame_spans_data_and_ignored_frames)
LT_BEGIN_AUTO_TEST(h3_ws_suite, coalesced_and_fragmented_messages_with_interleaved_ping)
    fixture f; LT_ASSERT(f.settings()); LT_ASSERT(f.request());
    auto wire = masked(1, "first"); auto append = [&](auto more) { wire.insert(wire.end(), more.begin(), more.end()); };
    append(masked(1, "hel", false)); append(masked(9, "p")); append(masked(0, "lo"));
    LT_ASSERT(f.data(wire));
    auto first = f.take(); auto second = f.take(); LT_ASSERT(first->has_value()); LT_ASSERT(second->has_value());
    LT_CHECK_EQ(text(**first), "first"); LT_CHECK_EQ(text(**second), "hello");
    LT_CHECK_EQ(f.reply().body, std::string("\x8a\x01p", 3));
LT_END_AUTO_TEST(coalesced_and_fragmented_messages_with_interleaved_ping)
LT_BEGIN_AUTO_TEST(h3_ws_suite, queued_data_resumes_after_route_upgrade)
    fixture f; LT_ASSERT(f.settings()); f.open(0);
    auto wire = h3test::headers(connect()); auto body = h3test::frame(0, masked(1, "queued"));
    wire.insert(wire.end(), body.begin(), body.end()); LT_ASSERT(f.feed(0, wire));
    f.executor.run_pending(); LT_ASSERT_EQ(f.sessions.size(), 1U);
    auto received = f.take(); LT_ASSERT(received->has_value()); LT_CHECK_EQ(text(**received), "queued");
LT_END_AUTO_TEST(queued_data_resumes_after_route_upgrade)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
