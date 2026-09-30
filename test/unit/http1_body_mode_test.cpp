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

// TASK-106 step 1: authoritative HTTP/1 body-mode computation unit
// tests. Pins detail::http1_body_mode::compute over heads parsed by the
// real http1_head_parser (the mode is computed once after take()):
//   - the accepted corpus (no body, fixed length, chunked) with the
//     close policy at none and the length carried exactly;
//   - the rejection corpus, each row of the rule table with its exact
//     typed outcome and close policy (DR-V3-006): protocol_error and
//     close_now for framing-ambiguity/malformed syntax, not_supported
//     and respond_then_close for an unsupported transfer coding.

#include <httpserver/detail/http1_body_mode.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <httpserver/detail/http1_head_parser.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;

using httpserver::detail::http1_body_kind;
using httpserver::detail::http1_body_mode;
using httpserver::detail::http1_close_policy;
using httpserver::detail::http1_head_budget;
using httpserver::detail::http1_head_parser;
using httpserver::detail::http1_head_state;
using httpserver::http::outcome_code;

// The production defaults.
http1_head_budget default_budget() {
    return http1_head_budget();
}

// Parses one request head with the production parser and computes its
// authoritative body mode (the composition the engine uses).
http1_body_mode mode_of(std::string_view bytes) {
    http1_head_parser p(default_budget());
    p.feed(bytes);
    if (p.state() != http1_head_state::complete) {
        throw std::runtime_error("expected one complete head");
    }
    return http1_body_mode::compute(p.take());
}

// True iff the head yields the expected accepted mode with the exact
// content length, a clean failure, and no close action.
bool accepts_as(std::string_view bytes, http1_body_kind kind,
                std::uint64_t content_length) {
    const http1_body_mode m = mode_of(bytes);
    return m.kind == kind
        && m.content_length == content_length
        && m.close_policy == http1_close_policy::none
        && m.failure.ok();
}

// True iff the head is rejected: kind rejected, the exact typed
// outcome with a diagnostic, and the exact close posture.
bool rejects_as(std::string_view bytes, outcome_code code,
                http1_close_policy policy) {
    const http1_body_mode m = mode_of(bytes);
    return m.kind == http1_body_kind::rejected
        && m.failure.code() == code
        && !m.failure.message().empty()
        && m.close_policy == policy;
}

}  // namespace

LT_BEGIN_SUITE(mode_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(mode_suite)

LT_BEGIN_AUTO_TEST(mode_suite, default_mode_is_bodyless)
    const http1_body_mode m;
    LT_CHECK(m.kind == http1_body_kind::none);
    LT_CHECK_EQ(m.content_length, 0u);
    LT_CHECK(m.failure.ok());
    LT_CHECK(m.close_policy == http1_close_policy::none);
LT_END_AUTO_TEST(default_mode_is_bodyless)

LT_BEGIN_AUTO_TEST(mode_suite, no_body_get_11)
    LT_CHECK(accepts_as("GET / HTTP/1.1\r\nHost: h\r\n\r\n",
                        http1_body_kind::none, 0));
LT_END_AUTO_TEST(no_body_get_11)

LT_BEGIN_AUTO_TEST(mode_suite, no_body_post_11)
    // POST without framing headers: no body either (the mode depends on
    // the fields, never on the method).
    LT_CHECK(accepts_as("POST /x HTTP/1.1\r\nHost: h\r\n\r\n",
                        http1_body_kind::none, 0));
LT_END_AUTO_TEST(no_body_post_11)

LT_BEGIN_AUTO_TEST(mode_suite, no_body_http_10)
    LT_CHECK(accepts_as("GET / HTTP/1.0\r\n\r\n",
                        http1_body_kind::none, 0));
LT_END_AUTO_TEST(no_body_http_10)

LT_BEGIN_AUTO_TEST(mode_suite, length_body_on_post)
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n\r\n",
        http1_body_kind::length, 5));
LT_END_AUTO_TEST(length_body_on_post)

LT_BEGIN_AUTO_TEST(mode_suite, length_zero_body)
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nContent-Length:0\r\n\r\n",
        http1_body_kind::length, 0));
LT_END_AUTO_TEST(length_zero_body)

LT_BEGIN_AUTO_TEST(mode_suite, length_ows_around_value)
    // The head parser trimmed the OWS already; the mode sees "5".
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nContent-Length:  5 \r\n\r\n",
        http1_body_kind::length, 5));
LT_END_AUTO_TEST(length_ows_around_value)

LT_BEGIN_AUTO_TEST(mode_suite, length_on_head_and_get)
    // The request method does not affect the request-body mode; the
    // HEAD no-body rule is response-side.
    LT_CHECK(accepts_as(
        "HEAD /x HTTP/1.1\r\nHost: h\r\nContent-Length: 9\r\n\r\n",
        http1_body_kind::length, 9));
    LT_CHECK(accepts_as(
        "GET /x HTTP/1.0\r\nContent-Length: 9\r\n\r\n",
        http1_body_kind::length, 9));
LT_END_AUTO_TEST(length_on_head_and_get)

LT_BEGIN_AUTO_TEST(mode_suite, length_huge_valid_value)
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\n"
        "Content-Length: 18446744073709551615\r\n\r\n",
        http1_body_kind::length, 18446744073709551615ULL));
LT_END_AUTO_TEST(length_huge_valid_value)

LT_BEGIN_AUTO_TEST(mode_suite, chunked_value_case_insensitive)
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\n",
        http1_body_kind::chunked, 0));
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: Chunked\r\n\r\n",
        http1_body_kind::chunked, 0));
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: CHUNKED\r\n\r\n",
        http1_body_kind::chunked, 0));
LT_END_AUTO_TEST(chunked_value_case_insensitive)

LT_BEGIN_AUTO_TEST(mode_suite, chunked_field_name_case_insensitive)
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\ntransfer-encoding: chunked\r\n\r\n",
        http1_body_kind::chunked, 0));
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTRANSFER-ENCODING: chunked\r\n\r\n",
        http1_body_kind::chunked, 0));
LT_END_AUTO_TEST(chunked_field_name_case_insensitive)

LT_BEGIN_AUTO_TEST(mode_suite, chunked_value_with_ows)
    LT_CHECK(accepts_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding:  chunked \r\n\r\n",
        http1_body_kind::chunked, 0));
LT_END_AUTO_TEST(chunked_value_with_ows)

LT_BEGIN_SUITE(reject_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(reject_suite)

LT_BEGIN_AUTO_TEST(reject_suite, te_and_cl_is_smuggling)
    // R1: both framings present, in either order: protocol_error,
    // close_now.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n"
        "Content-Length: 5\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n"
        "Transfer-Encoding: chunked\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_and_cl_is_smuggling)

LT_BEGIN_AUTO_TEST(reject_suite, te_on_http_10)
    // R2a: chunked exists only from HTTP/1.1.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_on_http_10)

LT_BEGIN_AUTO_TEST(reject_suite, te_empty_value)
    // R2b: an empty element inside the transfer-coding list.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding:\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked,\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_empty_value)

LT_BEGIN_AUTO_TEST(reject_suite, te_not_a_token)
    // R2b: every element must be a token (chunked;x=y carries a ';').
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked;x=y\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_not_a_token)

LT_BEGIN_AUTO_TEST(reject_suite, te_without_chunked)
    // R2c: no chunked element at all.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: identity\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: gzip\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_without_chunked)

LT_BEGIN_AUTO_TEST(reject_suite, te_chunked_not_last)
    // R2d: chunked must be the final element.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\n"
        "Transfer-Encoding: chunked, gzip\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_chunked_not_last)

LT_BEGIN_AUTO_TEST(reject_suite, te_chunked_more_than_once)
    // R2d: chunked exactly once.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\n"
        "Transfer-Encoding: chunked, chunked\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_chunked_more_than_once)

LT_BEGIN_AUTO_TEST(reject_suite, te_unsupported_coding_before_chunked)
    // R2e: a non-chunked token before the final chunked is a 501, so
    // respond_then_close — not the smuggling close_now.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\n"
        "Transfer-Encoding: gzip, chunked\r\n\r\n",
        http::outcome_code::not_supported,
        http1_close_policy::respond_then_close));
LT_END_AUTO_TEST(te_unsupported_coding_before_chunked)

LT_BEGIN_AUTO_TEST(reject_suite, te_split_across_field_lines)
    // Multiple TE field occurrences concatenate in wire order before
    // the rule table runs.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: gzip\r\n"
        "Transfer-Encoding: chunked\r\n\r\n",
        http::outcome_code::not_supported,
        http1_close_policy::respond_then_close));
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n"
        "Transfer-Encoding: chunked\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_split_across_field_lines)

LT_BEGIN_AUTO_TEST(reject_suite, cl_duplicate_identical)
    // R3a: even identical duplicates are refused (v3 declines the RFC
    // 9110 MAY-level combining; smuggling resistance).
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n"
        "Content-Length: 5\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n"
        "Content-Length: 6\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(cl_duplicate_identical)

LT_BEGIN_AUTO_TEST(reject_suite, cl_malformed_values)
    // R3b: every non-"1*DIGIT" spelling, an empty value, leading zeros
    // and a 20-digit overflow are protocol errors with close_now.
    const char* const bad[] = {
        "Content-Length: -1\r\n",
        "Content-Length: +5\r\n",
        "Content-Length: 5a\r\n",
        "Content-Length: 5 6\r\n",
        "Content-Length: a5\r\n",
        "Content-Length:\r\n",
        "Content-Length: 007\r\n",
        "Content-Length: 99999999999999999999\r\n",
    };
    for (const char* const field : bad) {
        const std::string head = std::string(
            "POST /x HTTP/1.1\r\nHost: h\r\n") + field + "\r\n";
        LT_CHECK(rejects_as(head, http::outcome_code::protocol_error,
                            http1_close_policy::close_now));
    }
LT_END_AUTO_TEST(cl_malformed_values)

LT_BEGIN_AUTO_TEST(reject_suite, te_and_cl_counts_every_cl_occurrence)
    // R1 fires on TE + any CL occurrence, even a malformed one: the
    // ambiguity is checked before the value is parsed.
    LT_CHECK(rejects_as(
        "POST /x HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n"
        "Content-Length:\r\n\r\n",
        http::outcome_code::protocol_error, http1_close_policy::close_now));
LT_END_AUTO_TEST(te_and_cl_counts_every_cl_occurrence)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
