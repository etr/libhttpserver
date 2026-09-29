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

// TASK-096 Cycle 1: transcript model + line parser unit tests.
//
// Pins the *.tseq transcript grammar used by the segmented-input parity
// runner: profile/case/send/send_hex/expect/option directives, escape
// handling (\r \n \t \\ \" \xHH), every expect kind, mask placement
// rules, and file:line diagnostics on every parse error. The parser is
// pure (no sockets, no libhttpserver) so these tests need no daemon.

#include "../parity/transcript.hpp"

#include <stdexcept>
#include <string>

#include "./littletest.hpp"

namespace {

using parity::expect_kind;
using parity::tcase;
using parity::transcript;
using parity::transcript_error;

// One failing parse, returning the thrown error.
transcript_error parse_fail(const std::string& text) {
    try {
        parity::parse_transcript(text, "sample.tseq");
    } catch (const transcript_error& e) {
        return e;
    }
    throw std::runtime_error("expected transcript_error but parse succeeded");
}

}  // namespace

LT_BEGIN_SUITE(transcript_parser_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(transcript_parser_suite)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, parses_hello_sample)
    const std::string sample =
        "# routing.tseq -- comment line\n"
        "\n"
        "profile routing_basic\n"
        "\n"
        "case get_hello\n"
        "send GET /hello HTTP/1.1\\r\\n\n"
        "send Host: 127.0.0.1\\r\\n\n"
        "send \\r\\n\n"
        "expect status 200\n"
        "expect header Content-Type: text/plain\n"
        "expect body \"OK\"\n"
        "expect framing content-length\n"
        "expect connection keep-alive\n";

    transcript t = parity::parse_transcript(sample, "sample.tseq");
    LT_CHECK_EQ(t.profile, "routing_basic");
    LT_CHECK_EQ(t.cases.size(), 1u);
    const tcase& c = t.cases[0];
    LT_CHECK_EQ(c.name, "get_hello");
    LT_CHECK_EQ(c.sends.size(), 3u);
    LT_CHECK_EQ(c.sends[0].bytes, "GET /hello HTTP/1.1\r\n");
    LT_CHECK_EQ(c.sends[2].bytes, "\r\n");
    LT_CHECK_EQ(c.expects.size(), 5u);
    LT_CHECK(c.expects[0].kind == expect_kind::status);
    LT_CHECK_EQ(c.expects[0].number, 200);
    LT_CHECK(c.expects[1].kind == expect_kind::header);
    LT_CHECK_EQ(c.expects[1].name, "Content-Type");
    LT_CHECK_EQ(c.expects[1].value, "text/plain");
    LT_CHECK(c.expects[2].kind == expect_kind::body);
    LT_CHECK_EQ(c.expects[2].value, "OK");
    LT_CHECK(c.expects[3].kind == expect_kind::framing);
    LT_CHECK_EQ(c.expects[3].value, "content-length");
    LT_CHECK(c.expects[4].kind == expect_kind::connection);
    LT_CHECK_EQ(c.expects[4].value, "keep-alive");
LT_END_AUTO_TEST(parses_hello_sample)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, default_case_options)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK_EQ(t.cases[0].read_timeout_ms, 5000);
LT_END_AUTO_TEST(default_case_options)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, option_read_timeout)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "option read_timeout_ms 250\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK_EQ(t.cases[0].read_timeout_ms, 250);
LT_END_AUTO_TEST(option_read_timeout)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, send_hex_segment)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send_hex 474554202f20485454502f312e310d0a\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK_EQ(t.cases[0].sends[0].bytes, "GET / HTTP/1.1\r\n");
LT_END_AUTO_TEST(send_hex_segment)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, escape_handling)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send tab\\there \\x41\\\\quote\\\" \\r\\n\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK_EQ(t.cases[0].sends[0].bytes, "tab\there A\\quote\" \r\n");
LT_END_AUTO_TEST(escape_handling)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, curl_transport_options)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "option transport curl\n"
        "option curl_tls true\n"
        "option curl_auth digest\n"
        "option curl_user bob:builder\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n"
        "expect status 200\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK_EQ(t.cases[0].transport, "curl");
    LT_CHECK(t.cases[0].curl_tls);
    LT_CHECK_EQ(t.cases[0].curl_auth, "digest");
    LT_CHECK_EQ(t.cases[0].curl_user, "bob:builder");
LT_END_AUTO_TEST(curl_transport_options)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, bad_transport_option_is_error)
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "option transport telepathy\n").line(), 3);
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "option curl_auth ntlm\n").line(), 3);
LT_END_AUTO_TEST(bad_transport_option_is_error)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, quoted_body_with_escapes)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n"
        "expect body \"line1\\nline2\\x21\"\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK_EQ(t.cases[0].expects[0].value, "line1\nline2!");
LT_END_AUTO_TEST(quoted_body_with_escapes)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, status_line_expect)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n"
        "expect status_line \"ICY 200 OK\"\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK(t.cases[0].expects[0].kind == expect_kind::status_line);
    LT_CHECK_EQ(t.cases[0].expects[0].value, "ICY 200 OK");
LT_END_AUTO_TEST(status_line_expect)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, header_absent_and_masks)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n"
        "expect header ~Set-Cookie\n"
        "expect header WWW-Authenticate: Basic realm=\"<*>\"\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK(t.cases[0].expects[0].kind == expect_kind::header_absent);
    LT_CHECK_EQ(t.cases[0].expects[0].name, "Set-Cookie");
    LT_CHECK(t.cases[0].expects[1].kind == expect_kind::header);
    LT_CHECK_EQ(t.cases[0].expects[1].value, "Basic realm=\"<*>\"");
LT_END_AUTO_TEST(header_absent_and_masks)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, header_order_expect)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n"
        "expect header_order Date Content-Type\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK(t.cases[0].expects[0].kind == expect_kind::header_order);
    LT_CHECK_EQ(t.cases[0].expects[0].order.size(), 2u);
    LT_CHECK_EQ(t.cases[0].expects[0].order[0], "Date");
    LT_CHECK_EQ(t.cases[0].expects[0].order[1], "Content-Type");
LT_END_AUTO_TEST(header_order_expect)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, body_hex_len_file_closer)
    const std::string sample =
        "profile p\n"
        "case c1\n"
        "send GET / HTTP/1.1\\r\\n\\r\\n\n"
        "expect body_hex deadBEEF\n"
        "expect body_len 17\n"
        "expect body_file test_content\n"
        "expect framing chunked\n"
        "expect closer server\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK(t.cases[0].expects[0].kind == expect_kind::body_hex);
    LT_CHECK_EQ(t.cases[0].expects[0].value, "deadBEEF");
    LT_CHECK(t.cases[0].expects[1].kind == expect_kind::body_len);
    LT_CHECK_EQ(t.cases[0].expects[1].number, 17);
    LT_CHECK(t.cases[0].expects[2].kind == expect_kind::body_file);
    LT_CHECK_EQ(t.cases[0].expects[2].name, "test_content");
    LT_CHECK(t.cases[0].expects[4].kind == expect_kind::closer);
    LT_CHECK_EQ(t.cases[0].expects[4].value, "server");
LT_END_AUTO_TEST(body_hex_len_file_closer)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, end_directive_and_multiple_cases)
    const std::string sample =
        "profile p\n"
        "case one\n"
        "send GET /one HTTP/1.1\\r\\n\\r\\n\n"
        "expect status 201\n"
        "end\n"
        "case two\n"
        "send GET /two HTTP/1.1\\r\\n\\r\\n\n"
        "expect status 202\n";
    transcript t = parity::parse_transcript(sample, "s.tseq");
    LT_CHECK_EQ(t.cases.size(), 2u);
    LT_CHECK_EQ(t.cases[0].name, "one");
    LT_CHECK_EQ(t.cases[1].name, "two");
    LT_CHECK_EQ(t.cases[0].expects[0].number, 201);
    LT_CHECK_EQ(t.cases[1].expects[0].number, 202);
LT_END_AUTO_TEST(end_directive_and_multiple_cases)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, option_outside_case_is_error)
    transcript_error e = parse_fail("option read_timeout_ms 100\n");
    LT_CHECK_EQ(e.line(), 1);
LT_END_AUTO_TEST(option_outside_case_is_error)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, error_diagnostics)
    LT_CHECK_EQ(parse_fail("case c1\n").line(), 1);
    LT_CHECK(parse_fail("case c1\n").what() ==
             std::string("sample.tseq:1: profile directive required before any case"));
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "profile p\n").line(), 2);
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "bogus directive\n").line(), 2);
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect status twohundred\n").line(), 3);
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "case c1\n").line(), 3);
    // duplicate expect of the same kind is legal (Nth response), so pin
    // a genuinely bad argument instead: body_len with a negative number
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect body_len -1\n").line(), 3);
LT_END_AUTO_TEST(error_diagnostics)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, missing_profile_at_eof)
    transcript_error e = parse_fail("# only comments\n\n");
    LT_CHECK(e.line() == 2);
LT_END_AUTO_TEST(missing_profile_at_eof)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, expect_arg_errors)
    // unknown expect kind
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect nonsense arg\n").line(), 3);
    // header without colon
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect header NoColonHere\n").line(), 3);
    // mask in body (masks allowed only in header values)
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect body \"a<*>b\"\n").line(), 3);
    // unquoted body
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect body unquoted\n").line(), 3);
    // bad framing token
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect framing telepathy\n").line(), 3);
    // bad connection token
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect connection maybe\n").line(), 3);
    // bad closer token
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect closer both\n").line(), 3);
    // bad hex
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "send_hex not-hex\n").line(), 3);
    // header_order with zero names
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "expect header_order\n").line(), 3);
LT_END_AUTO_TEST(expect_arg_errors)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, escape_error_reports_line)
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "send bad\\qescape\n").line(), 3);
    LT_CHECK_EQ(parse_fail(
        "profile p\n"
        "case c1\n"
        "send truncated\\x4\n").line(), 3);
LT_END_AUTO_TEST(escape_error_reports_line)

LT_BEGIN_AUTO_TEST(transcript_parser_suite, roundtrip_escape)
    const std::string original = "a\r\nb\tc\\d\"e\x01f";
    std::string escaped = parity::escape(original);
    LT_CHECK_EQ(parity::unescape(escaped, "s.tseq", 1), original);
LT_END_AUTO_TEST(roundtrip_escape)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
