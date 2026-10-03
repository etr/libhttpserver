/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#include <vector>
#include <httpserver/detail/websocket_codec.hpp>
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
using namespace ws_test;  // NOLINT(build/namespaces)
using httpserver::detail::websocket_codec;
LT_BEGIN_SUITE(websocket_codec_test_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(websocket_codec_test_suite)
LT_BEGIN_AUTO_TEST(websocket_codec_test_suite, codec_segmented_lengths_and_mask)
    for (std::size_t size : {0U, 1U, 125U, 126U, 65535U, 65536U}) {
        ws::options o; o.max_message_bytes = 70000; o.incoming_bytes = 140000; o.output_bytes = 70010;
        std::vector<std::byte> data(size, std::byte{0x9f});
        auto wire = frame(2, data);
        websocket_codec c(o);
        for (const auto b : wire) {
            auto r = c.feed(std::span(&b, 1));
            LT_CHECK(r.status.ok()); LT_CHECK_EQ(r.consumed, std::size_t{1});
        }
        auto m = c.pop(); LT_CHECK(m.has_value());
        LT_CHECK(m->data == data); LT_CHECK_EQ(c.incoming_bytes(), std::size_t{0});
    }
LT_END_AUTO_TEST(codec_segmented_lengths_and_mask)
LT_BEGIN_AUTO_TEST(websocket_codec_test_suite, codec_every_split_and_fragment_controls)
    auto first = frame(1, bytes("abc"), false);
    auto ping = frame(9, bytes("?"));
    auto last = frame(0, bytes("def"));
    std::vector<std::byte> wire = first;
    wire.insert(wire.end(), ping.begin(), ping.end());
    wire.insert(wire.end(), last.begin(), last.end());
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        websocket_codec c(small());
        for (auto part : {std::span(wire).first(split), std::span(wire).subspan(split)}) {
            while (!part.empty()) {
                auto r = c.feed(part); LT_CHECK(r.status.ok());
                LT_CHECK(r.consumed > 0); part = part.subspan(r.consumed);
                if (auto ctl = c.take_control()) LT_CHECK_EQ(ctl->opcode, 9U);
            }
        }
        auto m = c.pop(); LT_CHECK(m.has_value()); LT_CHECK(m->data == bytes("abcdef"));
    }
LT_END_AUTO_TEST(codec_every_split_and_fragment_controls)
LT_BEGIN_AUTO_TEST(websocket_codec_test_suite, codec_protocol_failures_are_sticky)
    std::vector<std::vector<std::byte>> bad = {
        frame(1, bytes("a"), true, false), frame(3, {}), frame(0, {}),
        frame(9, {}, false), frame(9, std::vector<std::byte>(126)),
        frame(8, bytes("x")), frame(8, bytes("\x03\xed")),
        {std::byte{0xc1}, std::byte{0x80}},
        {std::byte{0x82}, std::byte{0xfe}, std::byte{0}, std::byte{125}},
        {std::byte{0x82}, std::byte{0xff}, std::byte{0x80}, std::byte{0}, std::byte{0}, std::byte{0},
         std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}},
        {std::byte{0x82}, std::byte{0xff}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
         std::byte{0}, std::byte{0}, std::byte{0}, std::byte{126}}
    };
    for (auto& wire : bad) {
        websocket_codec c(small()); auto r = c.feed(wire);
        LT_CHECK(!r.status.ok()); LT_CHECK(c.feed({}).status.code() == r.status.code());
        LT_CHECK_EQ(c.incoming_bytes(), std::size_t{0});
    }
    websocket_codec c(small()); LT_CHECK(c.feed(frame(1, {}, false)).status.ok());
    LT_CHECK(!c.feed(frame(2, {})).status.ok());
LT_END_AUTO_TEST(codec_protocol_failures_are_sticky)
LT_BEGIN_AUTO_TEST(websocket_codec_test_suite, codec_admission_before_payload_and_empty_count)
    websocket_codec c(small()); auto wire = frame(2, bytes("123456789"));
    auto r = c.feed(std::span(wire).first(2));
    LT_CHECK(r.status.code() == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK_EQ(c.incoming_bytes(), std::size_t{0});
    websocket_codec q(small());
    LT_CHECK(q.feed(frame(2, {})).status.ok()); LT_CHECK(q.feed(frame(2, {})).status.ok());
    auto empty = frame(2, {}); auto blocked = q.feed(empty);
    LT_CHECK(blocked.blocked); LT_CHECK_EQ(blocked.consumed, std::size_t{6});
    LT_CHECK_EQ(q.incoming_messages(), std::size_t{2}); LT_CHECK(q.pop().has_value());
    LT_CHECK(q.feed({}).status.ok()); LT_CHECK_EQ(q.incoming_messages(), std::size_t{2});
LT_END_AUTO_TEST(codec_admission_before_payload_and_empty_count)
LT_BEGIN_AUTO_TEST(websocket_codec_test_suite, codec_aggregate_and_fragment_caps)
    websocket_codec c(small());
    LT_CHECK(c.feed(frame(2, bytes("12345678"))).status.ok());
    LT_CHECK(c.feed(frame(2, bytes("12345678"))).status.ok());
    auto wire = frame(2, bytes("a")); auto r = c.feed(wire);
    LT_CHECK(r.blocked); LT_CHECK_EQ(r.consumed, std::size_t{6});
    LT_CHECK_EQ(c.incoming_bytes(), std::size_t{16}); LT_CHECK(c.pop().has_value());
    LT_CHECK_EQ(c.feed(std::span(wire).subspan(r.consumed)).consumed, std::size_t{1});
    websocket_codec f(small()); LT_CHECK(f.feed(frame(2, bytes("1234"), false)).status.ok());
    LT_CHECK(f.feed(frame(0, bytes("56789"))).status.code() == httpserver::http::outcome_code::limit_exceeded);
    LT_CHECK_EQ(f.incoming_bytes(), std::size_t{0});
LT_END_AUTO_TEST(codec_aggregate_and_fragment_caps)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
