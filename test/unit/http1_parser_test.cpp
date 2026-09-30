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

// TASK-105: strict HTTP/1 start-line and header parser unit tests.
//
// Pins the private detail::http1_head_parser: single-shot and
// incremental parsing of request heads (PRD-V3N-REQ-004), the
// every-split matrix, malformed-syntax rejections with their close
// policies (DR-V3-006), the byte/field budgets, repeated-field order
// (PRD-V3N-REQ-017), and request-target handling (PRD-V3N-REQ-019).

#include <httpserver/detail/http1_head_parser.hpp>

#include <stdexcept>
#include <string>

#include "./littletest.hpp"

namespace {

using httpserver::detail::http1_close_policy;
using httpserver::detail::http1_head_budget;
using httpserver::detail::http1_head_parser;
using httpserver::detail::http1_head_state;
using httpserver::http::method;
using httpserver::http::method_id;
using httpserver::http::outcome_code;
using httpserver::http::protocol;
using httpserver::http::request_head;

// The production defaults: 1 MiB of head bytes, 256 field occurrences.
http1_head_budget default_budget() {
    return http1_head_budget();
}

// Full-value comparison of two parsed heads: every member the parser
// produces must agree.
bool heads_equal(const request_head& a, const request_head& b) {
    return a.raw_target == b.raw_target
        && a.route_path == b.route_path
        && a.request_method == b.request_method
        && a.request_protocol == b.request_protocol
        && a.head_fields == b.head_fields;
}

// Feeds everything at once and requires a complete head.
request_head parse_one(const std::string& bytes) {
    http1_head_parser p(default_budget());
    p.feed(bytes);
    if (p.state() != http1_head_state::complete) {
        throw std::runtime_error("expected one complete head");
    }
    return p.take();
}

// Sample heads exercised under every split.
const std::string MIN_10 = "GET / HTTP/1.0\r\n\r\n";
const std::string MIN_11 =
    "GET /a/b?q=1 HTTP/1.1\r\nHost: example.com\r\n\r\n";
const std::string EXT_METHOD =
    "PROPFIND /x HTTP/1.1\r\nHost: h\r\n\r\n";

// True iff `bytes` yields the identical head single-shot, under a
// two-way split at every offset (0..size inclusive), and bytewise.
bool parses_identically(const std::string& bytes) {
    const request_head reference = parse_one(bytes);
    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        http1_head_parser p(default_budget());
        p.feed(bytes.substr(0, split));
        p.feed(bytes.substr(split));
        if (p.state() != http1_head_state::complete) return false;
        if (!heads_equal(p.take(), reference)) return false;
    }
    http1_head_parser p(default_budget());
    for (const char c : bytes) p.feed(std::string(1, c));
    if (p.state() != http1_head_state::complete) return false;
    return heads_equal(p.take(), reference);
}

// True iff `bytes` yields the identical head under every three-way
// split (i, j).
bool parses_identically_three_way(const std::string& bytes) {
    const request_head reference = parse_one(bytes);
    for (std::size_t i = 0; i <= bytes.size(); ++i) {
        for (std::size_t j = i; j <= bytes.size(); ++j) {
            http1_head_parser p(default_budget());
            p.feed(bytes.substr(0, i));
            p.feed(bytes.substr(i, j - i));
            p.feed(bytes.substr(j));
            if (p.state() != http1_head_state::complete) return false;
            if (!heads_equal(p.take(), reference)) return false;
        }
    }
    return true;
}

// True iff a pipelined pair yields both heads in order under every
// two-way split of the combined byte stream.
bool pipelined_pair_under_every_split(const std::string& first,
                                      const std::string& second) {
    const std::string pair = first + second;
    http1_head_parser reference(default_budget());
    reference.feed(pair);
    if (reference.state() != http1_head_state::complete) return false;
    const request_head head1 = reference.take();
    if (reference.state() != http1_head_state::complete) return false;
    const request_head head2 = reference.take();
    for (std::size_t split = 0; split <= pair.size(); ++split) {
        http1_head_parser p(default_budget());
        p.feed(pair.substr(0, split));
        p.feed(pair.substr(split));
        if (p.state() != http1_head_state::complete) return false;
        if (!heads_equal(p.take(), head1)) return false;
        if (p.state() != http1_head_state::complete) return false;
        if (!heads_equal(p.take(), head2)) return false;
    }
    return true;
}

}  // namespace

LT_BEGIN_SUITE(http1_happy_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(http1_happy_suite)

LT_BEGIN_AUTO_TEST(http1_happy_suite, fresh_parser_is_empty)
    http1_head_parser p(default_budget());
    LT_CHECK(p.state() == http1_head_state::empty);
    LT_CHECK(p.close_policy() == http1_close_policy::none);
    LT_CHECK(p.failure().ok());
    LT_CHECK_EQ(p.residue_size(), 0u);
LT_END_AUTO_TEST(fresh_parser_is_empty)

LT_BEGIN_AUTO_TEST(http1_happy_suite, truncated_feed_stays_partial)
    http1_head_parser p(default_budget());
    p.feed("GET /a HTTP/1.1\r\nHost: h\r");
    LT_CHECK(p.state() == http1_head_state::partial);
    p.feed("\n\r");
    LT_CHECK(p.state() == http1_head_state::partial);
    p.feed("\n");
    LT_CHECK(p.state() == http1_head_state::complete);
LT_END_AUTO_TEST(truncated_feed_stays_partial)

LT_BEGIN_AUTO_TEST(http1_happy_suite, minimal_http_10_head)
    request_head h = parse_one("GET / HTTP/1.0\r\n\r\n");
    LT_CHECK(h.request_method == method::known(method_id::get));
    LT_CHECK(h.request_protocol == protocol::http_1_0);
    LT_CHECK_EQ(h.raw_target, "/");
    LT_CHECK_EQ(h.route_path, "/");
    LT_CHECK(h.head_fields.empty());
LT_END_AUTO_TEST(minimal_http_10_head)

LT_BEGIN_AUTO_TEST(http1_happy_suite, minimal_http_11_head_with_host)
    request_head h = parse_one(
        "GET /a/b?q=1 HTTP/1.1\r\nHost: example.com\r\n\r\n");
    LT_CHECK(h.request_method == method::known(method_id::get));
    LT_CHECK(h.request_protocol == protocol::http_1_1);
    LT_CHECK_EQ(h.raw_target, "/a/b?q=1");
    LT_CHECK_EQ(h.route_path, "/a/b");
    LT_CHECK_EQ(*h.head_fields.first("Host"), "example.com");
LT_END_AUTO_TEST(minimal_http_11_head_with_host)

LT_BEGIN_AUTO_TEST(http1_happy_suite, known_method_round_trip)
    request_head h = parse_one("POST /x HTTP/1.1\r\nHost: h\r\n\r\n");
    LT_CHECK(h.request_method == method::known(method_id::post));
LT_END_AUTO_TEST(known_method_round_trip)

LT_BEGIN_AUTO_TEST(http1_happy_suite, extension_method_round_trip)
    request_head h = parse_one("PROPFIND /x HTTP/1.1\r\nHost: h\r\n\r\n");
    LT_CHECK(h.request_method.is_extension());
    LT_CHECK_EQ(h.request_method.name(), "PROPFIND");
LT_END_AUTO_TEST(extension_method_round_trip)

LT_BEGIN_AUTO_TEST(http1_happy_suite, method_case_is_folded)
    request_head h = parse_one("get /x HTTP/1.1\r\nHost: h\r\n\r\n");
    LT_CHECK(h.request_method == method::known(method_id::get));
LT_END_AUTO_TEST(method_case_is_folded)

LT_BEGIN_SUITE(http1_split_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(http1_split_suite)

LT_BEGIN_AUTO_TEST(http1_split_suite, split_matrix_minimal_10)
    LT_CHECK(parses_identically(MIN_10));
LT_END_AUTO_TEST(split_matrix_minimal_10)

LT_BEGIN_AUTO_TEST(http1_split_suite, split_matrix_minimal_11)
    LT_CHECK(parses_identically(MIN_11));
LT_END_AUTO_TEST(split_matrix_minimal_11)

LT_BEGIN_AUTO_TEST(http1_split_suite, split_matrix_extension_method)
    LT_CHECK(parses_identically(EXT_METHOD));
LT_END_AUTO_TEST(split_matrix_extension_method)

LT_BEGIN_AUTO_TEST(http1_split_suite, three_way_split_two_smallest)
    LT_CHECK(parses_identically_three_way(MIN_10));
    LT_CHECK(parses_identically_three_way(MIN_11));
LT_END_AUTO_TEST(three_way_split_two_smallest)

LT_BEGIN_AUTO_TEST(http1_split_suite, pipelined_pair_under_every_split)
    LT_CHECK(pipelined_pair_under_every_split(MIN_10, MIN_11));
LT_END_AUTO_TEST(pipelined_pair_under_every_split)

LT_BEGIN_AUTO_TEST(http1_split_suite, residue_size_tracks_residue)
    http1_head_parser p(default_budget());
    p.feed(MIN_10);
    LT_CHECK(p.state() == http1_head_state::complete);
    LT_CHECK_EQ(p.residue_size(), 0u);
    p.feed(MIN_11);
    LT_CHECK_EQ(p.residue_size(), MIN_11.size());
    const request_head first = p.take();
    LT_CHECK_EQ(first.raw_target, "/");
    LT_CHECK(p.state() == http1_head_state::complete);
    LT_CHECK_EQ(p.residue_size(), 0u);
    const request_head second = p.take();
    LT_CHECK_EQ(second.raw_target, "/a/b?q=1");
    LT_CHECK(p.state() == http1_head_state::empty);
LT_END_AUTO_TEST(residue_size_tracks_residue)

LT_BEGIN_AUTO_TEST(http1_split_suite, bytewise_feed_matches_single_shot)
    const request_head reference = parse_one(MIN_11);
    http1_head_parser p(default_budget());
    for (const char c : MIN_11) p.feed(std::string(1, c));
    LT_CHECK(p.state() == http1_head_state::complete);
    LT_CHECK(heads_equal(p.take(), reference));
LT_END_AUTO_TEST(bytewise_feed_matches_single_shot)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
