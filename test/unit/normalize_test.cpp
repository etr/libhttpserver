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

// TASK-096 Cycle 3: normalizer unit tests.
//
// Pins expectation-vs-observation comparison: volatile-header elision
// (Date), <*> mask matching for header values, case-insensitive header
// names, absent-header assertions, relative header ordering, body
// assertions, framing assertions, and diff-message shape.

#include "../parity/normalize.hpp"

#include <string>

#include "./littletest.hpp"

namespace {

using parity::expectation;
using parity::expect_kind;
using parity::normalized_exchange;
using parity::normalize;
using parity::observed_header;
using parity::observed_response;

normalized_exchange sample_exchange() {
    observed_response r;
    r.raw_status_line = "HTTP/1.1 200 OK";
    r.status = 200;
    r.headers.push_back({"Date", "Mon, 28 Sep 2026 10:00:00 GMT"});
    r.headers.push_back({"content-type", "text/plain"});
    r.headers.push_back({"WWW-Authenticate",
                         "Digest realm=\"x\", nonce=\"abcdef123\""});
    r.body = "OK";
    r.framing = "content-length";
    return normalize(r);
}

expectation make_expect(expect_kind kind) {
    expectation e;
    e.kind = kind;
    e.line = 42;
    return e;
}

}  // namespace

LT_BEGIN_SUITE(normalize_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(normalize_suite)

LT_BEGIN_AUTO_TEST(normalize_suite, date_elided)
    normalized_exchange ex = sample_exchange();
    LT_CHECK(find_header(ex, "date") == nullptr);
LT_END_AUTO_TEST(date_elided)

LT_BEGIN_AUTO_TEST(normalize_suite, case_insensitive_lookup)
    normalized_exchange ex = sample_exchange();
    const observed_header* ct = find_header(ex, "CONTENT-TYPE");
    LT_CHECK(ct != nullptr);
    LT_CHECK_EQ(ct->value, "text/plain");
LT_END_AUTO_TEST(case_insensitive_lookup)

LT_BEGIN_AUTO_TEST(normalize_suite, mask_match)
    LT_CHECK(parity::mask_match("Digest realm=\"x\", nonce=\"<*>\"",
                                "Digest realm=\"x\", nonce=\"abcdef123\""));
    LT_CHECK(parity::mask_match("<*>", "anything at all"));
    LT_CHECK(parity::mask_match("", ""));
    LT_CHECK(parity::mask_match("pre<*>post", "preMIDDLEpost"));
    LT_CHECK(parity::mask_match("pre<*>post", "prepost"));
    LT_CHECK(!parity::mask_match("pre<*>post", "prepostX"));
    LT_CHECK(!parity::mask_match("nonce=\"<*>\"", "nonce=\"abc\" extra"));
LT_END_AUTO_TEST(mask_match)

LT_BEGIN_AUTO_TEST(normalize_suite, header_mask_expectation)
    normalized_exchange ex = sample_exchange();
    expectation e = make_expect(expect_kind::header);
    e.name = "WWW-Authenticate";
    e.value = "Digest realm=\"x\", nonce=\"<*>\"";
    parity::match_result m = parity::check_expectation(e, ex, ".");
    LT_CHECK(m.ok);
LT_END_AUTO_TEST(header_mask_expectation)

LT_BEGIN_AUTO_TEST(normalize_suite, header_mask_mismatch_diff)
    normalized_exchange ex = sample_exchange();
    expectation e = make_expect(expect_kind::header);
    e.name = "WWW-Authenticate";
    e.value = "Digest realm=\"y\", nonce=\"<*>\"";
    e.line = 17;
    parity::match_result m = parity::check_expectation(e, ex, ".");
    LT_CHECK(!m.ok);
    LT_CHECK(m.diff.find("WWW-Authenticate") != std::string::npos);
    LT_CHECK(m.diff.find("expect-line=17") != std::string::npos);
LT_END_AUTO_TEST(header_mask_mismatch_diff)

LT_BEGIN_AUTO_TEST(normalize_suite, header_absent_expectation)
    normalized_exchange ex = sample_exchange();
    expectation absent = make_expect(expect_kind::header_absent);
    absent.name = "Set-Cookie";
    LT_CHECK(parity::check_expectation(absent, ex, ".").ok);
    expectation present = make_expect(expect_kind::header_absent);
    present.name = "content-type";
    parity::match_result m = parity::check_expectation(present, ex, ".");
    LT_CHECK(!m.ok);
LT_END_AUTO_TEST(header_absent_expectation)

LT_BEGIN_AUTO_TEST(normalize_suite, header_order_check)
    normalized_exchange ex = sample_exchange();
    expectation e = make_expect(expect_kind::header_order);
    e.order = {"content-type", "WWW-Authenticate"};
    LT_CHECK(parity::check_expectation(e, ex, ".").ok);
    expectation reversed = make_expect(expect_kind::header_order);
    reversed.order = {"WWW-Authenticate", "content-type"};
    LT_CHECK(!parity::check_expectation(reversed, ex, ".").ok);
LT_END_AUTO_TEST(header_order_check)

LT_BEGIN_AUTO_TEST(normalize_suite, body_expectations)
    normalized_exchange ex = sample_exchange();
    expectation body = make_expect(expect_kind::body);
    body.value = "OK";
    LT_CHECK(parity::check_expectation(body, ex, ".").ok);

    expectation hex = make_expect(expect_kind::body_hex);
    hex.value = "4F4B";
    LT_CHECK(parity::check_expectation(hex, ex, ".").ok);

    expectation len = make_expect(expect_kind::body_len);
    len.number = 2;
    LT_CHECK(parity::check_expectation(len, ex, ".").ok);

    expectation wrong_len = make_expect(expect_kind::body_len);
    wrong_len.number = 3;
    LT_CHECK(!parity::check_expectation(wrong_len, ex, ".").ok);
LT_END_AUTO_TEST(body_expectations)

LT_BEGIN_AUTO_TEST(normalize_suite, body_file_expectation)
    normalized_exchange ex = sample_exchange();
    ex.body = "hello world\n";
    expectation e = make_expect(expect_kind::body_file);
    e.name = "normalize_body_fixture";
    parity::match_result m = parity::check_expectation(e, ex, NORMALIZE_FIXTURE_DIR);
    LT_CHECK(m.ok);
    e.name = "no_such_fixture_file";
    LT_CHECK(!parity::check_expectation(e, ex, NORMALIZE_FIXTURE_DIR).ok);
LT_END_AUTO_TEST(body_file_expectation)

LT_BEGIN_AUTO_TEST(normalize_suite, framing_and_status)
    normalized_exchange ex = sample_exchange();
    expectation framing = make_expect(expect_kind::framing);
    framing.value = "content-length";
    LT_CHECK(parity::check_expectation(framing, ex, ".").ok);
    expectation status = make_expect(expect_kind::status);
    status.number = 200;
    LT_CHECK(parity::check_expectation(status, ex, ".").ok);
    expectation bad_status = make_expect(expect_kind::status);
    bad_status.number = 201;
    parity::match_result m = parity::check_expectation(bad_status, ex, ".");
    LT_CHECK(!m.ok);
    LT_CHECK(m.diff.find("status") != std::string::npos);
LT_END_AUTO_TEST(framing_and_status)

LT_BEGIN_AUTO_TEST(normalize_suite, status_line_expectation)
    normalized_exchange ex = sample_exchange();
    expectation e = make_expect(expect_kind::status_line);
    e.value = "HTTP/1.1 200 OK";
    LT_CHECK(parity::check_expectation(e, ex, ".").ok);
    expectation icy = make_expect(expect_kind::status_line);
    icy.value = "ICY 200 OK";
    LT_CHECK(!parity::check_expectation(icy, ex, ".").ok);
LT_END_AUTO_TEST(status_line_expectation)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
