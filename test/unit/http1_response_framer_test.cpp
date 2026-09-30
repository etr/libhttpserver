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

// TASK-107: HTTP/1 response framer tests (PRD-V3N-REQ-004/026,
// DR-V3-006). The framer is one pure byte producer per response:
//
//   step 3 pins the status line (mirrored version token, RFC 9110
//   section 15 reason phrases, empty phrase for unassigned codes) and
//   the ordered field serialization (handler occurrences verbatim in
//   entries() order, typed validation before any byte is appended),
//   with a parse-back through the parity response-frame parser;
//
//   later steps pin the engine fields (Date/Connection/framing), the
//   no-body strip rules, interim responses, the ICY status token, body
//   framing and trailers, and the transcript-corpus replay.
//
// Replay scope note (plan section 6): the corpus cases replayed here
// are the response-head/field subset; out-of-scope cases (auth_digest,
// tls/ip_controls, route-adapter cases) belong to TASK-108/110/114.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <parity/response_frame.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;

using httpserver::detail::http1_response_framer;
using httpserver::detail::http1_response_mode;
using parity::observed_response;
using parity::response_frame_parser;

http::request_head request(http::method_id id, http::protocol version) {
    http::request_head head;
    head.raw_target = "/x";
    head.route_path = "/x";
    head.request_method = http::method::known(id);
    head.request_protocol = version;
    return head;
}

http::request_head get_11() {
    return request(http::method_id::get, http::protocol::http_1_1);
}

http::request_head get_10() {
    return request(http::method_id::get, http::protocol::http_1_0);
}

// Drives one full response head through a fresh framer and returns the
// emitted bytes (empty on a typed failure).
std::string emit_head(const http::request_head& req, std::uint16_t code,
                      const http::fields& fields,
                      const http1_response_framer::clock_source& clock = {},
                      std::string_view status_token = {}) {
    http1_response_framer framer(clock, status_token);
    std::string out;
    if (!framer.start_head(out, req, http::status::from_code(code), fields)
             .ok()) {
        return {};
    }
    return out;
}

// Parses one complete response (head bytes + raw body) and returns the
// single observed response. feed() hands over every response it
// completed, so the returns are accumulated.
observed_response parse_back(const std::string& head, const std::string& body) {
    response_frame_parser parser;
    parser.feed(head);
    std::vector<observed_response> done = parser.feed(body);
    for (observed_response& r : parser.finish()) {
        done.push_back(std::move(r));
    }
    return done.empty() ? observed_response{} : done.front();
}

}  // namespace

LT_BEGIN_SUITE(framer_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(framer_suite)

LT_BEGIN_AUTO_TEST(framer_suite, status_line_404_byte_exact)
    http::fields f;
    f.append("Content-Length", "9");
    const std::string out = emit_head(get_11(), 404, f);
    LT_CHECK(out == "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\n");
LT_END_AUTO_TEST(status_line_404_byte_exact)

LT_BEGIN_AUTO_TEST(framer_suite, status_line_mirrors_request_version)
    http::fields f;
    f.append("Content-Length", "2");
    const std::string out = emit_head(get_10(), 200, f);
    LT_CHECK(out == "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n");
LT_END_AUTO_TEST(status_line_mirrors_request_version)

LT_BEGIN_AUTO_TEST(framer_suite, reason_phrase_spot_checks)
    using httpserver::detail::http1_reason_phrase;
    LT_CHECK(http1_reason_phrase(200) == "OK");
    LT_CHECK(http1_reason_phrase(404) == "Not Found");
    LT_CHECK(http1_reason_phrase(405) == "Method Not Allowed");
    LT_CHECK(http1_reason_phrase(500) == "Internal Server Error");
    LT_CHECK(http1_reason_phrase(431) == "Request Header Fields Too Large");
    LT_CHECK(http1_reason_phrase(505) == "HTTP Version Not Supported");
    LT_CHECK(http1_reason_phrase(100) == "Continue");
    LT_CHECK(http1_reason_phrase(101) == "Switching Protocols");
    LT_CHECK(http1_reason_phrase(204) == "No Content");
    LT_CHECK(http1_reason_phrase(304) == "Not Modified");
    // Unassigned valid codes carry an empty phrase.
    LT_CHECK(http1_reason_phrase(299).empty());
    LT_CHECK(http1_reason_phrase(498).empty());
    LT_CHECK(http1_reason_phrase(598).empty());
LT_END_AUTO_TEST(reason_phrase_spot_checks)

LT_BEGIN_AUTO_TEST(framer_suite, unknown_code_line_has_empty_phrase)
    // status-line = version SP code SP [reason] CRLF: the SP before the
    // (absent) phrase is kept, so the line ends with a bare SP.
    http::fields f;
    f.append("Content-Length", "0");
    const std::string out = emit_head(get_11(), 299, f);
    LT_CHECK(out == "HTTP/1.1 299 \r\nContent-Length: 0\r\n\r\n");
LT_END_AUTO_TEST(unknown_code_line_has_empty_phrase)

LT_BEGIN_AUTO_TEST(framer_suite, fields_emit_verbatim_in_order)
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("X-A", "1");
    f.append("X-A", "2");
    f.append("Content-Length", "3");
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "X-A: 1\r\n"
                    "X-A: 2\r\n"
                    "Content-Length: 3\r\n"
                    "\r\n");
LT_END_AUTO_TEST(fields_emit_verbatim_in_order)

LT_BEGIN_AUTO_TEST(framer_suite, repeated_name_preserves_first_spelling)
    // fields keeps the first-seen spelling of a name for every
    // occurrence; the framer renders exactly what entries() reports.
    http::fields f;
    f.append("x-a", "1");
    f.append("X-A", "2");
    f.append("Content-Length", "0");
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "x-a: 1\r\n"
                    "x-a: 2\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n");
LT_END_AUTO_TEST(repeated_name_preserves_first_spelling)

LT_BEGIN_AUTO_TEST(framer_suite, bad_field_name_fails_before_any_byte)
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("X A", "no token");  // SP is not a token byte
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out.empty());

    http1_response_framer framer;
    std::string out2;
    const http::outcome bad = framer.start_head(
        out2, get_11(), http::status::from_code(200), f);
    LT_CHECK(bad.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!bad.message().empty());
    LT_CHECK(out2.empty());
LT_END_AUTO_TEST(bad_field_name_fails_before_any_byte)

LT_BEGIN_AUTO_TEST(framer_suite, ctl_in_field_value_fails_before_any_byte)
    http::fields f;
    f.append("X-A", std::string("bad\rvalue"));
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out.empty());
    // NUL is a CTL too.
    http::fields nul;
    nul.append("X-A", std::string("bad\0value", 9));
    LT_CHECK(emit_head(get_11(), 200, nul).empty());
LT_END_AUTO_TEST(ctl_in_field_value_fails_before_any_byte)

LT_BEGIN_AUTO_TEST(framer_suite, htab_in_field_value_is_ows_not_ctl)
    http::fields f;
    f.append("X-A", "a\tb");
    f.append("Content-Length", "0");
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\nX-A: a\tb\r\nContent-Length: 0\r\n\r\n");
LT_END_AUTO_TEST(htab_in_field_value_is_ows_not_ctl)

LT_BEGIN_AUTO_TEST(framer_suite, invalid_status_fails_without_bytes)
    http::fields f;
    f.append("Content-Length", "9");
    LT_CHECK(emit_head(get_11(), 0, f).empty());
    LT_CHECK(emit_head(get_11(), 600, f).empty());
    LT_CHECK(emit_head(get_11(), 99, f).empty());
LT_END_AUTO_TEST(invalid_status_fails_without_bytes)

LT_BEGIN_AUTO_TEST(framer_suite, ambiguous_framing_fails_without_bytes)
    http::fields f;
    f.append("Content-Length", "9");
    f.append("Transfer-Encoding", "chunked");
    LT_CHECK(emit_head(get_11(), 200, f).empty());
LT_END_AUTO_TEST(ambiguous_framing_fails_without_bytes)

LT_BEGIN_AUTO_TEST(framer_suite, double_start_fails_typed)
    http::fields f;
    f.append("Content-Length", "9");
    http1_response_framer framer;
    std::string out;
    LT_CHECK(framer.start_head(out, get_11(), http::status::from_code(200),
                               f).ok());
    const http::outcome again = framer.start_head(
        out, get_11(), http::status::from_code(404), f);
    LT_CHECK(again.code() == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(double_start_fails_typed)

LT_BEGIN_AUTO_TEST(framer_suite, mode_reflects_the_computed_framing)
    http::fields f;
    f.append("Content-Length", "9");
    http1_response_framer framer;
    std::string out;
    LT_CHECK(framer.start_head(out, get_11(), http::status::from_code(200),
                               f).ok());
    LT_CHECK(framer.mode().kind ==
             httpserver::detail::http1_response_body_kind::length);
    LT_CHECK_EQ(framer.mode().content_length, 9u);
LT_END_AUTO_TEST(mode_reflects_the_computed_framing)

LT_BEGIN_AUTO_TEST(framer_suite, parse_back_full_length_response)
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "9");
    const std::string head = emit_head(get_11(), 404, f);
    const observed_response r = parse_back(head, "Not Found");
    LT_CHECK(r.status == 404);
    LT_CHECK(r.raw_status_line == "HTTP/1.1 404 Not Found");
    LT_CHECK(r.framing == "content-length");
    LT_CHECK(r.body == "Not Found");
    LT_CHECK(r.headers.size() == 2);
    LT_CHECK(r.headers[0].name == "Content-Type");
    LT_CHECK(r.headers[0].value == "text/plain");
    LT_CHECK(r.headers[1].name == "Content-Length");
    LT_CHECK(r.headers[1].value == "9");
LT_END_AUTO_TEST(parse_back_full_length_response)

LT_BEGIN_AUTO_TEST(framer_suite, parse_back_mirrored_10_version)
    http::fields f;
    f.append("Content-Length", "3");
    const std::string head = emit_head(get_10(), 200, f);
    const observed_response r = parse_back(head, "abc");
    LT_CHECK(r.status == 200);
    LT_CHECK(r.raw_status_line == "HTTP/1.0 200 OK");
    LT_CHECK(r.framing == "content-length");
    LT_CHECK(r.body == "abc");
LT_END_AUTO_TEST(parse_back_mirrored_10_version)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
