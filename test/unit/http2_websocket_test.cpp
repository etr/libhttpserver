/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <string>
#include <utility>
#include <vector>
#include <optional>
#include "./http2_websocket_fixture.hpp"
#include "./littletest.hpp"
using namespace h2ws;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(h2_ws_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(h2_ws_suite)
LT_BEGIN_AUTO_TEST(h2_ws_suite, split_masked_messages_and_early_data_reach_one_stream_codec)
    fixture f; LT_ASSERT(f.start());
    auto opening = h2test::frame(1, 4, 1, h2test::encode(f.encoder, connect()));
    auto message = masked(1, "hello");
    h2test::append(opening, h2test::frame(0, 0, 1, {message.begin(), message.begin() + 3}));
    LT_ASSERT(h2test::feed(*f.engine, opening)); f.executor.run_pending(); LT_ASSERT_EQ(f.sessions.size(), 1u);
    std::optional<h::websocket::receive_result> received;
    h::spawn(f.executor, f.sessions[0].receive(), [&](auto result) { received.emplace(std::move(result.value())); });
    f.executor.run_pending(); LT_CHECK(!received);
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(0, 0, 1, {message.begin() + 3, message.end()})));
    f.executor.run_pending(); LT_ASSERT(received); LT_CHECK(received->status.ok());
    LT_CHECK_EQ(std::string(reinterpret_cast<const char*>(received->value->data.data()), received->value->data.size()), "hello");
LT_END_AUTO_TEST(split_masked_messages_and_early_data_reach_one_stream_codec)
LT_BEGIN_AUTO_TEST(h2_ws_suite, immediate_server_output_follows_handshake_and_siblings_remain_independent)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open(1)); LT_ASSERT(f.open(3)); LT_ASSERT_EQ(f.sessions.size(), 2u);
    const std::string text = "reply";
    for (auto& session : f.sessions) LT_CHECK(session.try_send(h::websocket::message_kind::text, std::as_bytes(std::span(text))).status.ok());
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(1, 5, 5, h2test::encode(f.encoder, h2test::get()))));
    auto wire = f.output(); LT_CHECK(h2test::resets(wire).empty());
    auto frames = h2test::frames(wire); std::vector<std::uint32_t> heads;
    for (const auto& frame : frames) {
        if (frame.type == 1) heads.push_back(frame.stream);
        if (frame.type == 0) LT_CHECK(std::find(heads.begin(), heads.end(), frame.stream) != heads.end());
    }
    LT_CHECK(data(wire, 1) == std::vector<std::uint8_t>({129, 5, 'r', 'e', 'p', 'l', 'y'}));
    LT_CHECK(data(wire, 3) == data(wire, 1));
    auto replies = h2test::responses(wire, true); LT_ASSERT_EQ(replies.size(), 3u); LT_CHECK_EQ(replies.back().stream, 5u);
LT_END_AUTO_TEST(immediate_server_output_follows_handshake_and_siblings_remain_independent)
LT_BEGIN_AUTO_TEST(h2_ws_suite, ping_uses_shared_codec_and_produces_unmasked_pong)
    fixture f; LT_ASSERT(f.start()); LT_ASSERT(f.open()); f.output();
    LT_ASSERT(h2test::feed(*f.engine, h2test::frame(0, 0, 1, masked(9, "ping"))));
    LT_CHECK(data(f.output(), 1) == std::vector<std::uint8_t>({138, 4, 'p', 'i', 'n', 'g'}));
LT_END_AUTO_TEST(ping_uses_shared_codec_and_produces_unmasked_pong)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
