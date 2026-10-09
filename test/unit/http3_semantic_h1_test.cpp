/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string>
#include "../support/http3_semantic_h1.hpp"
#include "./littletest.hpp"
LT_BEGIN_SUITE(http3_semantic_h1_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(http3_semantic_h1_suite)
LT_BEGIN_AUTO_TEST(http3_semantic_h1_suite, initial_reply_field_cannot_satisfy_trailer_parity)
    const std::string wire = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nx-reply: done\r\n\r\n3\r\nabc\r\n0\r\n\r\n";
    parity::response_frame_parser parser;
    const auto responses = parser.feed(wire);
    LT_ASSERT_EQ(responses.size(), 1u);
    LT_ASSERT(!parser.failed());

    const auto fields = h3semantic::observe_h1_fields(responses.front(), wire);

    LT_CHECK(fields.trailers.empty());
    LT_ASSERT_EQ(fields.headers.size(), 1u);
    LT_CHECK_EQ(fields.headers.front().first, "x-reply");
    LT_CHECK_EQ(fields.headers.front().second, "done");
LT_END_AUTO_TEST(initial_reply_field_cannot_satisfy_trailer_parity)
LT_BEGIN_AUTO_TEST(http3_semantic_h1_suite, actual_chunk_trailer_satisfies_trailer_parity)
    const std::string wire = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nx-repeat: a\r\nx-repeat: b\r\n\r\n3\r\nabc\r\n0\r\nx-reply: done\r\n\r\n";
    parity::response_frame_parser parser;
    LT_ASSERT(parser.feed(wire.substr(0, wire.size() - 1)).empty());
    const auto responses = parser.feed(wire.substr(wire.size() - 1));
    LT_ASSERT_EQ(responses.size(), 1u);
    LT_ASSERT(!parser.failed());

    const auto fields = h3semantic::observe_h1_fields(responses.front(), wire);

    LT_ASSERT_EQ(fields.trailers.size(), 1u);
    LT_CHECK_EQ(fields.trailers.front().first, "x-reply");
    LT_CHECK_EQ(fields.trailers.front().second, "done");
    LT_ASSERT_EQ(fields.headers.size(), 2u);
    LT_CHECK_EQ(fields.headers[0].second, "a");
    LT_CHECK_EQ(fields.headers[1].second, "b");
LT_END_AUTO_TEST(actual_chunk_trailer_satisfies_trailer_parity)
LT_BEGIN_AUTO_TEST(http3_semantic_h1_suite, repeated_name_retains_initial_and_trailing_blocks)
    const std::string wire = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nx-reply: initial\r\n\r\n0\r\nx-reply: done\r\nx-other: trailing\r\n\r\n";
    parity::response_frame_parser parser;
    const auto responses = parser.feed(wire);
    LT_ASSERT_EQ(responses.size(), 1u);
    LT_ASSERT(!parser.failed());

    const auto fields = h3semantic::observe_h1_fields(responses.front(), wire);

    LT_ASSERT_EQ(fields.headers.size(), 1u);
    LT_CHECK_EQ(fields.headers.front().first, "x-reply");
    LT_CHECK_EQ(fields.headers.front().second, "initial");
    LT_ASSERT_EQ(fields.trailers.size(), 2u);
    LT_CHECK_EQ(fields.trailers[0].second, "done");
    LT_CHECK_EQ(fields.trailers[1].first, "x-other");
    LT_CHECK_EQ(fields.trailers[1].second, "trailing");
LT_END_AUTO_TEST(repeated_name_retains_initial_and_trailing_blocks)
LT_BEGIN_AUTO_TEST(http3_semantic_h1_suite, content_length_reply_field_is_an_initial_field)
    const std::string wire = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nx-reply: done\r\n\r\nabc";
    parity::response_frame_parser parser;
    const auto responses = parser.feed(wire);
    LT_ASSERT_EQ(responses.size(), 1u);
    LT_ASSERT(!parser.failed());

    const auto fields = h3semantic::observe_h1_fields(responses.front(), wire);

    LT_CHECK(fields.trailers.empty());
    LT_ASSERT_EQ(fields.headers.size(), 1u);
    LT_CHECK_EQ(fields.headers.front().first, "x-reply");
LT_END_AUTO_TEST(content_length_reply_field_is_an_initial_field)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
