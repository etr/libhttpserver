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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-096 Cycle 2: response framer unit tests.
//
// Pins the incremental byte-stream -> observed_response converter used
// by the transcript runner: Content-Length framing, chunked framing with
// trailers, until-close framing, bodiless statuses (1xx/204/304), the
// split-at-every-byte-point robustness loop, pipelined responses, and
// error (not hang) reporting for invalid framing.

#include "../parity/response_frame.hpp"

#include <string>
#include <vector>

#include "./littletest.hpp"

namespace {

using parity::observed_response;
using parity::response_frame_parser;

// Feed all bytes at once; collect every completed response.
std::vector<observed_response> parse_all(const std::string& bytes) {
    response_frame_parser p;
    return p.feed(bytes);
}

observed_response parse_one(const std::string& bytes) {
    std::vector<observed_response> out = parse_all(bytes);
    if (out.size() != 1) throw std::runtime_error("expected exactly one response");
    return out[0];
}

// Feed one byte at a time and concatenate results; the observed set must
// be identical to a single-shot feed.
std::vector<observed_response> parse_bytewise(const std::string& bytes) {
    response_frame_parser p;
    std::vector<observed_response> out;
    for (char ch : bytes) {
        std::vector<observed_response> more = p.feed(std::string(1, ch));
        out.insert(out.end(), more.begin(), more.end());
    }
    return out;
}

const char* CL_200 =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "OK";

const char* CHUNKED =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "4\r\nWiki\r\n"
    "5\r\npedia\r\n"
    "E\r\n in\r\n\r\nchunks.\r\n"
    "0\r\n"
    "X-Trailer: t\r\n"
    "\r\n";

}  // namespace

LT_BEGIN_SUITE(response_frame_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(response_frame_suite)

LT_BEGIN_AUTO_TEST(response_frame_suite, content_length_response)
    observed_response r = parse_one(CL_200);
    LT_CHECK_EQ(r.status, 200);
    LT_CHECK_EQ(r.raw_status_line, "HTTP/1.1 200 OK");
    LT_CHECK_EQ(r.headers.size(), 2u);
    LT_CHECK_EQ(r.headers[0].name, "Content-Type");
    LT_CHECK_EQ(r.headers[0].value, "text/plain");
    LT_CHECK_EQ(r.body, "OK");
    LT_CHECK_EQ(r.framing, "content-length");
LT_END_AUTO_TEST(content_length_response)

LT_BEGIN_AUTO_TEST(response_frame_suite, content_length_case_insensitive_header)
    observed_response r = parse_one(
        "HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nOK");
    LT_CHECK_EQ(r.framing, "content-length");
    LT_CHECK_EQ(r.body, "OK");
LT_END_AUTO_TEST(content_length_case_insensitive_header)

LT_BEGIN_AUTO_TEST(response_frame_suite, chunked_with_trailers)
    observed_response r = parse_one(CHUNKED);
    LT_CHECK_EQ(r.status, 200);
    LT_CHECK_EQ(r.body, "Wikipedia in\r\n\r\nchunks.");
    LT_CHECK_EQ(r.framing, "chunked");
    // Trailers are recorded as headers after the body chunks.
    bool found_trailer = false;
    for (const auto& h : r.headers) {
        if (h.name == "X-Trailer" && h.value == "t") found_trailer = true;
    }
    LT_CHECK(found_trailer);
LT_END_AUTO_TEST(chunked_with_trailers)

LT_BEGIN_AUTO_TEST(response_frame_suite, bodiless_statuses)
    observed_response r204 = parse_one("HTTP/1.1 204 No Content\r\n\r\n");
    LT_CHECK_EQ(r204.status, 204);
    LT_CHECK_EQ(r204.framing, "none");
    LT_CHECK_EQ(r204.body, "");

    observed_response r101 = parse_one(
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n");
    LT_CHECK_EQ(r101.status, 101);
    LT_CHECK_EQ(r101.framing, "none");
LT_END_AUTO_TEST(bodiless_statuses)

LT_BEGIN_AUTO_TEST(response_frame_suite, until_close_framing)
    response_frame_parser p;
    std::vector<observed_response> out =
        p.feed("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nbodybytes");
    LT_CHECK_EQ(out.size(), 0u);  // incomplete until EOF
    out = p.finish();
    LT_CHECK_EQ(out.size(), 1u);
    LT_CHECK_EQ(out[0].body, "bodybytes");
    LT_CHECK_EQ(out[0].framing, "none");
LT_END_AUTO_TEST(until_close_framing)

LT_BEGIN_AUTO_TEST(response_frame_suite, pipelined_three_responses)
    std::string bytes;
    for (int i = 0; i < 3; ++i) bytes += CL_200;
    std::vector<observed_response> out = parse_all(bytes);
    LT_CHECK_EQ(out.size(), 3u);
    for (const auto& r : out) {
        LT_CHECK_EQ(r.status, 200);
        LT_CHECK_EQ(r.body, "OK");
    }
LT_END_AUTO_TEST(pipelined_three_responses)

LT_BEGIN_AUTO_TEST(response_frame_suite, split_at_every_point)
    // Every possible split of the byte stream across two feeds must give
    // the same result as a single-shot feed.
    const std::string bytes = std::string(CL_200) + CHUNKED;
    std::vector<observed_response> whole = parse_all(bytes);
    LT_CHECK_EQ(whole.size(), 2u);
    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        response_frame_parser p;
        std::vector<observed_response> out = p.feed(bytes.substr(0, split));
        std::vector<observed_response> more = p.feed(bytes.substr(split));
        out.insert(out.end(), more.begin(), more.end());
        bool same = out.size() == whole.size();
        if (same) {
            for (std::size_t i = 0; i < out.size(); ++i) {
                same = same && out[i].status == whole[i].status
                    && out[i].raw_status_line == whole[i].raw_status_line
                    && out[i].body == whole[i].body
                    && out[i].framing == whole[i].framing
                    && out[i].headers.size() == whole[i].headers.size();
            }
        }
        LT_CHECK(same);
    }
LT_END_AUTO_TEST(split_at_every_point)

LT_BEGIN_AUTO_TEST(response_frame_suite, bytewise_feed_matches_single_shot)
    std::vector<observed_response> whole = parse_all(CHUNKED);
    std::vector<observed_response> bytewise = parse_bytewise(CHUNKED);
    LT_CHECK_EQ(bytewise.size(), whole.size());
    LT_CHECK_EQ(bytewise[0].body, whole[0].body);
    LT_CHECK_EQ(bytewise[0].framing, whole[0].framing);
LT_END_AUTO_TEST(bytewise_feed_matches_single_shot)

LT_BEGIN_AUTO_TEST(response_frame_suite, truncated_content_length_is_error)
    response_frame_parser t;
    t.feed("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nonly4");
    std::vector<observed_response> out = t.finish();
    LT_CHECK_EQ(out.size(), 0u);
    LT_CHECK(!t.error().empty());
LT_END_AUTO_TEST(truncated_content_length_is_error)

LT_BEGIN_AUTO_TEST(response_frame_suite, invalid_chunk_size_is_error)
    response_frame_parser p;
    p.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    p.feed("zzz\r\n");
    LT_CHECK(!p.error().empty());
LT_END_AUTO_TEST(invalid_chunk_size_is_error)

LT_BEGIN_AUTO_TEST(response_frame_suite, oversized_declared_body_is_error)
    response_frame_parser p;
    p.feed("HTTP/1.1 200 OK\r\nContent-Length: 999999999\r\n\r\nshort");
    LT_CHECK(!p.error().empty());
LT_END_AUTO_TEST(oversized_declared_body_is_error)

LT_BEGIN_AUTO_TEST(response_frame_suite, bad_status_line_is_error)
    response_frame_parser p;
    p.feed("NOT-HTTP\r\n\r\n");
    LT_CHECK(!p.error().empty());
LT_END_AUTO_TEST(bad_status_line_is_error)

LT_BEGIN_AUTO_TEST(response_frame_suite, incremental_pipelined_feed)
    response_frame_parser p;
    std::vector<observed_response> out = p.feed(std::string(CL_200).substr(0, 20));
    LT_CHECK_EQ(out.size(), 0u);
    out = p.feed(std::string(CL_200).substr(20) + CHUNKED);
    LT_CHECK_EQ(out.size(), 2u);
    LT_CHECK_EQ(out[0].status, 200);
    LT_CHECK_EQ(out[1].framing, "chunked");
LT_END_AUTO_TEST(incremental_pipelined_feed)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
