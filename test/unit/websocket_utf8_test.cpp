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

#include <httpserver/detail/websocket_codec.hpp>
#include "./websocket_test_helpers.hpp"
#include "./littletest.hpp"
using namespace ws_test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(websocket_utf8_test_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(websocket_utf8_test_suite)
LT_BEGIN_AUTO_TEST(websocket_utf8_test_suite, utf8_across_fragments_and_every_chunk)
    auto text = bytes("A\xc2\xa2\xe2\x82\xac\xf0\x90\x8d\x88");
    for (std::size_t split = 0; split <= text.size(); ++split) {
        ws::options o; o.max_message_bytes = 16; o.incoming_bytes = 32;
        httpserver::detail::websocket_codec c(o);
        for (auto wire : {frame(1, std::span(text).first(split), false), frame(0, std::span(text).subspan(split))}) {
            for (auto b : wire) LT_CHECK(c.feed(std::span(&b, 1)).status.ok());
        }
        auto m = c.pop(); LT_CHECK(m.has_value()); LT_CHECK(m->data == text);
    }
LT_END_AUTO_TEST(utf8_across_fragments_and_every_chunk)
LT_BEGIN_AUTO_TEST(websocket_utf8_test_suite, utf8_invalid_text_and_close_binary_bypasses)
    for (auto bad : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xe2\x82", "\xf5\x80\x80\x80"}) {
        httpserver::detail::websocket_codec c(small());
        LT_CHECK(c.feed(frame(1, bytes(bad))).status.code() == httpserver::http::outcome_code::protocol_error);
        httpserver::detail::websocket_codec b(small()); LT_CHECK(b.feed(frame(2, bytes(bad))).status.ok());
        httpserver::detail::websocket_codec close(small());
        auto payload = bytes("\x03\xe8"); auto reason = bytes(bad); payload.insert(payload.end(), reason.begin(), reason.end());
        LT_CHECK(!close.feed(frame(8, payload)).status.ok());
    }
LT_END_AUTO_TEST(utf8_invalid_text_and_close_binary_bypasses)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
