/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/http2_request_head.hpp>
#include "./http2_websocket_fixture.hpp"
#include "./littletest.hpp"
using namespace h2ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h2_ws_handshake_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h2_ws_handshake_suite)
LT_BEGIN_AUTO_TEST(h2_ws_handshake_suite, extended_connect_preserves_method_and_normalizes_origin_path)
    http::request_head head;
    LT_ASSERT(h::detail::http2_convert_request(connect("/items/../chat?q=1"), head));
    LT_CHECK(head.request_method.id() == http::method_id::connect);
    LT_CHECK_EQ(head.raw_target, "/items/../chat?q=1"); LT_CHECK_EQ(head.route_path, "/chat");
    LT_CHECK(!head.head_fields.first(":protocol")); LT_CHECK_EQ(head.head_fields.first("host").value(), "example.test");
LT_END_AUTO_TEST(extended_connect_preserves_method_and_normalizes_origin_path)
LT_BEGIN_AUTO_TEST(h2_ws_handshake_suite, malformed_pseudo_fields_and_hop_by_hop_fields_are_stream_errors)
    auto base = connect();
    for (unsigned i = 0; i < 8; ++i) {
        auto fields = base;
        switch (i) {
            case 0: fields.push_back({":protocol", "websocket"}); break;
            case 1: fields[0].value = "GET"; break;
            case 2: fields.erase(fields.begin() + 2); break;
            case 3: fields[3].value = ""; break;
            case 4: fields[4].value = "*"; break;
            case 5: fields.push_back({"connection", "upgrade"}); break;
            case 6: fields.push_back({"upgrade", "websocket"}); break;
            case 7: fields[1].value = "bad protocol"; break;
        }
        http::request_head head; LT_CHECK(!h::detail::http2_convert_request(fields, head));
    }
LT_END_AUTO_TEST(malformed_pseudo_fields_and_hop_by_hop_fields_are_stream_errors)
LT_BEGIN_AUTO_TEST(h2_ws_handshake_suite, server_advertisement_allows_connect_without_peer_setting_or_ack)
    fixture f;
    auto settings = f.engine->output(); LT_ASSERT_EQ(settings[3], 4u);
    bool advertised = false;
    for (std::size_t at = 9; at + 6 <= settings.size(); at += 6) {
        if (settings[at] == 0 && settings[at + 1] == 8) advertised = settings[at + 5] == 1;
    }
    LT_CHECK(advertised); LT_ASSERT(f.start());
    f.options.subprotocols = {"chat"};
    auto fields = connect(); fields.push_back({"sec-websocket-protocol", "other, chat"});
    fields.push_back({"sec-websocket-extensions", "permessage-deflate"});
    LT_ASSERT(f.open(1, fields)); LT_ASSERT_EQ(f.sessions.size(), 1u);
    auto replies = h2test::responses(f.output(), true); LT_ASSERT_EQ(replies.size(), 1u);
    LT_CHECK_EQ(replies[0].fields[0].value, "200"); LT_CHECK(!replies[0].end_stream);
    LT_ASSERT_EQ(replies[0].fields.size(), 2u); LT_CHECK_EQ(replies[0].fields[1].name, "sec-websocket-protocol");
    LT_CHECK_EQ(replies[0].fields[1].value, "chat");
LT_END_AUTO_TEST(server_advertisement_allows_connect_without_peer_setting_or_ack)
LT_BEGIN_AUTO_TEST(h2_ws_handshake_suite, peer_setting_does_not_authorize_opening_before_server_advertisement)
    for (bool continuation : {false, true}) {
        fixture f; auto wire = h2test::preface(); h2test::append(wire, h2test::frame(4, 0, 0, h2test::setting(8, 1)));
        LT_ASSERT(h2test::feed(*f.engine, wire));
        auto block = h2test::encode(f.encoder, connect());
        if (continuation) {
            LT_ASSERT(h2test::feed(*f.engine, h2test::frame(1, 0, 1, {block.begin(), block.begin() + 1})));
            auto advertised = f.engine->output(); LT_ASSERT(!advertised.empty()); f.engine->advance_output(advertised.size());
            LT_ASSERT(h2test::feed(*f.engine, h2test::frame(9, 4, 1, {block.begin() + 1, block.end()})));
        } else {
            LT_ASSERT(h2test::feed(*f.engine, h2test::frame(1, 4, 1, block)));
        }
        f.executor.run_pending(); LT_CHECK(f.sessions.empty());
        auto resets = h2test::resets(f.output()); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].code, 1u);
    }
LT_END_AUTO_TEST(peer_setting_does_not_authorize_opening_before_server_advertisement)
LT_BEGIN_AUTO_TEST(h2_ws_handshake_suite, handshake_refusal_leaves_ordinary_response_available)
    for (unsigned i = 0; i < 7; ++i) {
        fixture f; LT_ASSERT(f.start()); auto fields = connect();
        switch (i) {
            case 0: fields.back().value = "12"; break;
            case 1: fields.push_back({"origin", "https://denied.test"}); f.options.allowed_origins = {"https://allowed.test"}; break;
            case 2: f.options.allow_absent_origin = false; break;
            case 3: f.options.require_subprotocol = true; break;
            case 4: fields[2].value = "ftp"; break;
            case 5: fields.push_back({"content-length", "0"}); break;
            case 6: fields.push_back({"sec-websocket-protocol", "chat, chat"}); break;
        }
        LT_ASSERT(f.open(1, fields)); LT_CHECK(f.sessions.empty()); LT_CHECK_EQ(f.refusals, 1u);
        auto wire = f.output(); LT_CHECK(h2test::resets(wire).empty());
        auto replies = h2test::responses(wire); LT_ASSERT_EQ(replies.size(), 1u); LT_CHECK(replies[0].fields[0].value != "200");
    }
LT_END_AUTO_TEST(handshake_refusal_leaves_ordinary_response_available)
LT_BEGIN_AUTO_TEST(h2_ws_handshake_suite, opening_frame_eligibility_is_fixed_before_fragmented_payload_finishes)
    for (std::size_t prefix : {5u, 10u}) {
        fixture f; LT_ASSERT(h2test::feed(*f.engine, h2test::preface()));
        auto wire = h2test::frame(1, 4, 1, h2test::encode(f.encoder, connect()));
        LT_ASSERT(h2test::feed(*f.engine, std::span(wire).first(prefix)));
        auto advertisement = f.engine->output(); LT_ASSERT(!advertisement.empty()); f.engine->advance_output(advertisement.size());
        LT_ASSERT(h2test::feed(*f.engine, std::span(wire).subspan(prefix))); f.executor.run_pending();
        LT_CHECK(f.sessions.empty()); auto resets = h2test::resets(f.output()); LT_ASSERT_EQ(resets.size(), 1u); LT_CHECK_EQ(resets[0].code, 1u);
    }
LT_END_AUTO_TEST(opening_frame_eligibility_is_fixed_before_fragmented_payload_finishes)
LT_BEGIN_AUTO_TEST(h2_ws_handshake_suite, invalid_codec_caps_and_timeouts_fail_engine_configuration)
    for (unsigned i = 0; i < 3; ++i) {
        h::detail::http2_request_limits limits;
        if (i == 0) limits.websocket.incoming_messages = SIZE_MAX;
        if (i == 1) limits.websocket.max_message_bytes = limits.websocket.incoming_bytes + 1;
        if (i == 2) limits.timeouts.ws_close = std::chrono::milliseconds::zero();
        fixture f(limits); LT_ASSERT(f.engine->failure()); LT_CHECK(f.engine->failure()->outcome == http::outcome_code::invalid_argument);
    }
LT_END_AUTO_TEST(invalid_codec_caps_and_timeouts_fail_engine_configuration)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
