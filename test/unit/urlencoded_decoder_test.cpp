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

// TASK-116 step 1: the strict incremental urlencoded body decoder
// (PRD-V3N-REQ-021/022, v2 semantics parity per PRD-V3N-REQ-038,
// DR-V3-001). The matrix pins:
//   - pair splitting (first '=' separates; '=' inside a value is
//     data), arrival order of repeated names, and the first-value
//     rule (the first entry a name maps to is the one lookup sees);
//   - '+' as 0x20 and strict case-insensitive %HH escapes, including
//     %00 and %2F, with a decoded %26 provably NOT re-entering the
//     pair state machine;
//   - escapes SPLIT ACROSS FEED BOUNDARIES -- the classic
//     urlencoded-parser bug -- over every split point of a body;
//   - the TASK-116 deltas: a malformed (incomplete or non-hex) escape
//     fails typed, including one truncated at end of body;
//   - token shapes (bare name, nameless value, empty value, empty
//     tokens skipped, trailing separator, empty body);
//   - raw NUL and high-bit bytes passing through verbatim (values are
//     length-carrying strings);
//   - the bounded admission contract: byte and field caps with
//     exactly-at-cap success, and storage provably stopping at the
//     failing feed (later pairs never materialize).

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/forms_urlencoded.hpp>
#include <httpserver/http/outcome.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
using httpserver::detail::urlencoded_decoder;

using entry = std::pair<std::string, std::string>;

constexpr std::uint64_t default_bytes = 65536;
constexpr std::uint64_t default_fields = 64;

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string hex_byte(unsigned char c) {
    const char digits[] = "0123456789abcdef";
    std::string out;
    out.push_back(digits[c >> 4]);
    out.push_back(digits[c & 0x0f]);
    return out;
}

std::string quote(const std::string& raw) {
    std::string out;
    for (const unsigned char c : raw) {
        if (c >= 0x20 && c < 0x7f) {
            out.push_back(static_cast<char>(c));
        } else {
            out += "\\x" + hex_byte(c);
        }
    }
    return out;
}

std::string render(const std::vector<entry>& fields) {
    std::string out = "[";
    bool first = true;
    for (const entry& e : fields) {
        if (!first) out += ", ";
        first = false;
        out += "(" + quote(e.first) + "," + quote(e.second) + ")";
    }
    out += "]";
    return out;
}

// "" iff the decoded field set equals the expected one.
std::string fields_diff(const std::vector<entry>& got,
                        const std::vector<entry>& expected) {
    if (got == expected) return "";
    return "got " + render(got) + " want " + render(expected);
}

// One-shot decode through a fresh decoder; "" iff feed+finish succeed
// and the fields equal @p expected.
std::string decodes_to(const std::string& body, std::uint64_t max_bytes,
                       std::uint64_t max_fields,
                       const std::vector<entry>& expected) {
    urlencoded_decoder d(max_bytes, max_fields);
    const http::outcome fed = d.feed(bytes_of(body));
    if (!fed.ok()) return "feed failed: " + fed.message();
    const http::outcome done = d.finish();
    if (!done.ok()) return "finish failed: " + done.message();
    return fields_diff(d.take_fields(), expected);
}

// The same body fed in chunks of @p chunk bytes must decode exactly
// like the one-shot form (escapes survive feed boundaries).
std::string segmented_decodes_to(const std::string& body, std::size_t chunk,
                                 std::uint64_t max_bytes,
                                 std::uint64_t max_fields,
                                 const std::vector<entry>& expected) {
    urlencoded_decoder d(max_bytes, max_fields);
    for (std::size_t at = 0; at < body.size(); at += chunk) {
        std::size_t n = chunk;
        if (at + n > body.size()) n = body.size() - at;
        const http::outcome fed = d.feed(bytes_of(body.substr(at, n)));
        if (!fed.ok()) {
            return "chunked feed at " + std::to_string(at) + " failed: "
                + fed.message();
        }
    }
    const http::outcome done = d.finish();
    if (!done.ok()) return "chunked finish failed: " + done.message();
    return fields_diff(d.take_fields(), expected);
}

// Every split point of @p body (feed the first k bytes, then the
// remainder) must decode exactly like the one-shot form.
std::string every_split_decodes_to(const std::string& body,
                                   const std::vector<entry>& expected) {
    for (std::size_t k = 0; k <= body.size(); ++k) {
        urlencoded_decoder d(default_bytes, default_fields);
        const http::outcome first = d.feed(bytes_of(body.substr(0, k)));
        if (!first.ok()) {
            return "first feed (k=" + std::to_string(k)
                + ") failed: " + first.message();
        }
        const http::outcome rest =
            d.feed(bytes_of(body.substr(k)));
        if (!rest.ok()) {
            return "second feed (k=" + std::to_string(k)
                + ") failed: " + rest.message();
        }
        const http::outcome done = d.finish();
        if (!done.ok()) {
            return "finish (k=" + std::to_string(k)
                + ") failed: " + done.message();
        }
        const std::string diff = fields_diff(d.take_fields(), expected);
        if (!diff.empty()) return "split k=" + std::to_string(k) + ": " + diff;
    }
    return "";
}

// The one-shot decode must fail with @p code; "" iff it does, with the
// stored prefix equal to @p stored (storage stopped at the failure).
std::string fails_with(const std::string& body, http::outcome_code code,
                       std::uint64_t max_bytes, std::uint64_t max_fields,
                       const std::vector<entry>& stored) {
    urlencoded_decoder d(max_bytes, max_fields);
    const http::outcome fed = d.feed(bytes_of(body));
    const http::outcome observed = fed.ok() ? d.finish() : fed;
    if (observed.ok()) return "decode unexpectedly succeeded";
    if (observed.code() != code) {
        return "failure code " + std::to_string(
                   static_cast<int>(observed.code()))
            + " want " + std::to_string(static_cast<int>(code));
    }
    return fields_diff(d.take_fields(), stored);
}

// "" iff the first failing feed stores @p stored AND the failure is
// sticky: later feeds and finish return the same typed failure, with
// storage unchanged.
std::string failure_is_sticky(const std::string& body,
                              http::outcome_code code,
                              std::uint64_t max_bytes,
                              std::uint64_t max_fields,
                              const std::vector<entry>& stored) {
    urlencoded_decoder d(max_bytes, max_fields);
    const http::outcome fed = d.feed(bytes_of(body));
    if (fed.ok()) return "feed unexpectedly succeeded";
    if (fed.code() != code) return "first failure code mismatch";
    const http::outcome again = d.feed(bytes_of("later=1"));
    if (again.code() != code) return "second feed not sticky";
    const http::outcome done = d.finish();
    if (done.code() != code) return "finish not sticky";
    return fields_diff(d.take_fields(), stored);
}

}  // namespace

LT_BEGIN_SUITE(urlencoded_decoder_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(urlencoded_decoder_suite)

// (1) Pairs split at the FIRST '='; a later '=' is value data.
// Repeated names append in arrival order; the first entry of a name
// is the value a first-occurrence lookup observes (v2 get_arg order).
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, pairs_arrive_in_order)
    LT_CHECK(decodes_to("a=1&b=two", default_bytes, default_fields,
                        {entry("a", "1"), entry("b", "two")}).empty());
    LT_CHECK(decodes_to("k=1&k=2&k=3", default_bytes, default_fields,
                        {entry("k", "1"), entry("k", "2"),
                         entry("k", "3")}).empty());
    LT_CHECK(decodes_to("a=b=c", default_bytes, default_fields,
                        {entry("a", "b=c")}).empty());
LT_END_AUTO_TEST(pairs_arrive_in_order)

// (2) '+' decodes to 0x20 (names and values); strict case-insensitive
// hex escapes decode to the raw byte, including NUL and '/'.
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, plus_and_escape_decoding)
    LT_CHECK(decodes_to("a=b+c", default_bytes, default_fields,
                        {entry("a", "b c")}).empty());
    LT_CHECK(decodes_to("a+b=1", default_bytes, default_fields,
                        {entry("a b", "1")}).empty());
    LT_CHECK(decodes_to("p=%2F&q=%2f", default_bytes, default_fields,
                        {entry("p", "/"), entry("q", "/")}).empty());
    LT_CHECK(decodes_to(std::string("n=%00"), default_bytes, default_fields,
                        {entry("n", std::string(1, '\0'))}).empty());
LT_END_AUTO_TEST(plus_and_escape_decoding)

// (3) A decoded %26 is DATA, never a separator: decoded bytes never
// re-enter the pair state machine.
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, decoded_separator_is_data)
    LT_CHECK(decodes_to("a=%26b&c=2", default_bytes, default_fields,
                        {entry("a", "&b"), entry("c", "2")}).empty());
    // A decoded '+' (%2B) stays '+', and a decoded '=' (%3D) in a
    // name never switches to value mode.
    LT_CHECK(decodes_to("x=%2B1", default_bytes, default_fields,
                        {entry("x", "+1")}).empty());
    LT_CHECK(decodes_to("a%3Db=1", default_bytes, default_fields,
                        {entry("a=b", "1")}).empty());
LT_END_AUTO_TEST(decoded_separator_is_data)

// (4) Escapes SPLIT ACROSS FEED BOUNDARIES: every split point of the
// body (including one inside a %HH run, and one right after the '%')
// decodes identically to the one-shot form.
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, escapes_survive_feed_splits)
    LT_CHECK(every_split_decodes_to(
        "q=%2F&r=%41", {entry("q", "/"), entry("r", "A")}).empty());
    LT_CHECK(segmented_decodes_to("a=%2F&b=%2f", 1, default_bytes,
                                  default_fields,
                                  {entry("a", "/"), entry("b", "/")})
                 .empty());
    LT_CHECK(segmented_decodes_to("a=%2F&b=%2f", 3, default_bytes,
                                  default_fields,
                                  {entry("a", "/"), entry("b", "/")})
                 .empty());
LT_END_AUTO_TEST(escapes_survive_feed_splits)

// (5) The TASK-116 delta: a malformed escape -- non-hex digit, escape
// truncated at end of body, escape cut by a separator -- fails typed
// (v2 passed these bytes through literally).
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, malformed_escapes_reject)
    LT_CHECK(fails_with("a=%G1", http::outcome_code::invalid_argument,
                        default_bytes, default_fields, {}).empty());
    LT_CHECK(fails_with("a=%2", http::outcome_code::invalid_argument,
                        default_bytes, default_fields, {}).empty());
    LT_CHECK(fails_with("a=%", http::outcome_code::invalid_argument,
                        default_bytes, default_fields, {}).empty());
    LT_CHECK(fails_with("a=1&b=%2", http::outcome_code::invalid_argument,
                        default_bytes, default_fields,
                        {entry("a", "1")}).empty());
    LT_CHECK(fails_with("a=1&%&b=2", http::outcome_code::invalid_argument,
                        default_bytes, default_fields,
                        {entry("a", "1")}).empty());
    // An escape truncated at end of body fails at finish(); the split
    // form fails the same way no matter where the boundary fell.
    const std::string truncated("a=%2");
    for (std::size_t k = 0; k <= truncated.size(); ++k) {
        urlencoded_decoder d(default_bytes, default_fields);
        LT_CHECK(d.feed(bytes_of(truncated.substr(0, k))).ok());
        LT_CHECK(d.feed(bytes_of(truncated.substr(k))).ok());
        LT_CHECK(d.finish().code()
                 == http::outcome_code::invalid_argument);
    }
LT_END_AUTO_TEST(malformed_escapes_reject)

// (6) Token shapes: a token with no '=' is a name with an empty
// value; a token starting with '=' is an empty name; empty tokens are
// skipped; an empty body yields no fields.
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, token_shapes)
    LT_CHECK(decodes_to("a", default_bytes, default_fields,
                        {entry("a", "")}).empty());
    LT_CHECK(decodes_to("=b", default_bytes, default_fields,
                        {entry("", "b")}).empty());
    LT_CHECK(decodes_to("a=", default_bytes, default_fields,
                        {entry("a", "")}).empty());
    LT_CHECK(decodes_to("a&&b", default_bytes, default_fields,
                        {entry("a", ""), entry("b", "")}).empty());
    LT_CHECK(decodes_to("a=1&", default_bytes, default_fields,
                        {entry("a", "1")}).empty());
    LT_CHECK(decodes_to("&a=1", default_bytes, default_fields,
                        {entry("a", "1")}).empty());
    LT_CHECK(decodes_to("", default_bytes, default_fields, {}).empty());
    // A one-escape name is a field, not an empty token.
    LT_CHECK(decodes_to("%41", default_bytes, default_fields,
                        {entry("A", "")}).empty());
LT_END_AUTO_TEST(token_shapes)

// (7) Raw NUL and high-bit bytes pass through verbatim: values are
// length-carrying strings, so only %HH and '+' are translated.
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, raw_bytes_pass_through)
    std::string body(1, '\xfe');
    body += '=';
    body.append("\x00\xff\x7f", 3);
    LT_CHECK(decodes_to(body, default_bytes, default_fields,
                        {entry(std::string(1, '\xfe'),
                               std::string("\x00\xff\x7f", 3))})
                 .empty());
LT_END_AUTO_TEST(raw_bytes_pass_through)

// (8) Field-count cap: exactly-at-cap succeeds; the pair that would
// exceed it fails typed -- including one flushed by finish(). Skipped
// empty tokens never consume field budget.
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, field_cap_boundary)
    LT_CHECK(decodes_to("a=1&b=2", default_bytes, 2,
                        {entry("a", "1"), entry("b", "2")}).empty());
    LT_CHECK(fails_with("a=1&b=2&c=3", http::outcome_code::limit_exceeded,
                        default_bytes, 2,
                        {entry("a", "1"), entry("b", "2")}).empty());
    // The over-cap pair pending at end of body trips at finish().
    LT_CHECK(fails_with("a=1&b=2", http::outcome_code::limit_exceeded,
                        default_bytes, 1, {entry("a", "1")}).empty());
    LT_CHECK(decodes_to("a=1&&&&&", default_bytes, 1,
                        {entry("a", "1")}).empty());
LT_END_AUTO_TEST(field_cap_boundary)

// (9) Byte cap on RAW body bytes: exactly-at-cap succeeds; one past
// fails typed -- including through many single-byte feeds, where the
// storage stops at the failing feed.
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, byte_cap_boundary)
    LT_CHECK(decodes_to("a=1&b", 5, default_fields,
                        {entry("a", "1"), entry("b", "")}).empty());
    LT_CHECK(fails_with("a=1&b=", http::outcome_code::limit_exceeded, 5,
                        default_fields, {entry("a", "1")}).empty());
    LT_CHECK(segmented_decodes_to("a=1&b", 1, 5, default_fields,
                                  {entry("a", "1"), entry("b", "")})
                 .empty());
    // Single-byte feeds: the sixth byte of a 5-byte cap fails typed.
    urlencoded_decoder d(5, default_fields);
    const std::string body("a=1&b=2");
    for (std::size_t i = 0; i < 5; ++i) {
        LT_CHECK(d.feed(bytes_of(body.substr(i, 1))).ok());
    }
    LT_CHECK(d.feed(bytes_of("=")).code()
             == http::outcome_code::limit_exceeded);
    // The pending "b" token never flushed -- its "=" tripped the cap
    // before any separator or finish could materialize it -- so the
    // stored prefix is exactly the one-shot sibling's.
    const std::string diff =
        fields_diff(d.take_fields(), {entry("a", "1")});
    if (!diff.empty()) {
        std::cerr << "[byte cap] " << diff << std::endl;
    }
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(byte_cap_boundary)

// (10) Storage provably stops at the failing feed: pairs after the
// malformed or over-cap one never materialize, and the failure is
// sticky across later feeds and finish().
LT_BEGIN_AUTO_TEST(urlencoded_decoder_suite, storage_stops_at_failure)
    LT_CHECK(failure_is_sticky(
                 "a=1&b=%zz&c=3", http::outcome_code::invalid_argument,
                 default_bytes, default_fields,
                 {entry("a", "1")}).empty());
    LT_CHECK(failure_is_sticky(
                 "a=1&b=2&c=3", http::outcome_code::limit_exceeded,
                 default_bytes, 1, {entry("a", "1")}).empty());
LT_END_AUTO_TEST(storage_stops_at_failure)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
