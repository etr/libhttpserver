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
//   step 4 pins the engine fields (Date only with a clock source and
//   in the exact IMF-fixdate form, Connection per the keep-alive
//   verdict, the canonical framing field appended last), the no-body
//   strip rules (204/1xx strip CL+TE, 304 strips TE, HEAD verbatim),
//   the interim-response bytes, and the ICY status token;
//
//   later steps pin body framing and trailers and the transcript-corpus
//   replay.
//
// Replay scope note (plan section 6): the corpus cases replayed here
// are the response-head/field subset; out-of-scope cases (auth_digest,
// tls/ip_controls, route-adapter cases) belong to TASK-108/110/114.

#include <chrono>
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
    // HTTP/1.0 defaults to close, so the engine appends the Connection
    // header in front of the framing field.
    http::fields f;
    f.append("Content-Length", "2");
    const std::string out = emit_head(get_10(), 200, f);
    LT_CHECK(out == "HTTP/1.0 200 OK\r\n"
                    "Connection: close\r\n"
                    "Content-Length: 2\r\n"
                    "\r\n");
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

LT_BEGIN_SUITE(engine_fields_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(engine_fields_suite)

// The RFC 7231 section 7.1.1.1 example instant, whose IMF-fixdate is
// the format pin below.
http1_response_framer::clock_source fixed_clock() {
    const std::chrono::system_clock::time_point instant{
        std::chrono::seconds(784111777)};
    return {[instant] { return instant; }};
}

LT_BEGIN_AUTO_TEST(engine_fields_suite, date_format_is_imf_fixdate)
    const std::string date = httpserver::detail::format_imf_fixdate(
        std::chrono::system_clock::time_point(
            std::chrono::seconds(784111777)));
    LT_CHECK(date == "Sun, 06 Nov 1994 08:49:37 GMT");
LT_END_AUTO_TEST(date_format_is_imf_fixdate)

LT_BEGIN_AUTO_TEST(engine_fields_suite, date_before_framing_field)
    // Engine order: Date, Connection, then exactly one framing field —
    // the canonical Content-Length is emitted last and the handler's
    // occurrence is suppressed (its validated value is re-emitted).
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "5");
    const std::string out = emit_head(get_11(), 200, f, fixed_clock());
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n");
LT_END_AUTO_TEST(date_before_framing_field)

LT_BEGIN_AUTO_TEST(engine_fields_suite, date_omitted_without_clock)
    http::fields f;
    f.append("Content-Length", "5");
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out.find("Date:") == std::string::npos);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n");
LT_END_AUTO_TEST(date_omitted_without_clock)

LT_BEGIN_AUTO_TEST(engine_fields_suite, handler_date_wins)
    http::fields f;
    f.append("Date", "Wed, 21 Oct 2015 07:28:00 GMT");
    f.append("Content-Length", "0");
    const std::string out = emit_head(get_11(), 200, f, fixed_clock());
    LT_CHECK(out.find("Sun, 06 Nov 1994") == std::string::npos);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "Date: Wed, 21 Oct 2015 07:28:00 GMT\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n");
LT_END_AUTO_TEST(handler_date_wins)

LT_BEGIN_AUTO_TEST(engine_fields_suite, connection_close_appended)
    http::request_head req = get_11();
    req.head_fields.append("Connection", "close");
    http::fields f;
    f.append("Content-Length", "5");
    const std::string out = emit_head(req, 200, f, fixed_clock());
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                    "Connection: close\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n");
LT_END_AUTO_TEST(connection_close_appended)

LT_BEGIN_AUTO_TEST(engine_fields_suite, http_10_keepalive_appended)
    http::request_head req = get_10();
    req.head_fields.append("Connection", "keep-alive");
    http::fields f;
    f.append("Content-Length", "2");
    const std::string out = emit_head(req, 200, f, fixed_clock());
    LT_CHECK(out == "HTTP/1.0 200 OK\r\n"
                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                    "Connection: keep-alive\r\n"
                    "Content-Length: 2\r\n"
                    "\r\n");
LT_END_AUTO_TEST(http_10_keepalive_appended)

LT_BEGIN_AUTO_TEST(engine_fields_suite, http_11_keepalive_appends_nothing)
    http::fields f;
    f.append("Content-Length", "2");
    const std::string out = emit_head(get_11(), 200, f, fixed_clock());
    LT_CHECK(out.find("Connection") == std::string::npos);
LT_END_AUTO_TEST(http_11_keepalive_appends_nothing)

LT_BEGIN_AUTO_TEST(engine_fields_suite, handler_connection_field_stripped)
    // The engine owns the Connection header (documented v3 policy
    // delta): handler occurrences are never emitted verbatim.
    http::fields f;
    f.append("Connection", "keep-alive, Upgrade");
    f.append("Content-Length", "2");
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out.find("Connection") == std::string::npos);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n");
LT_END_AUTO_TEST(handler_connection_field_stripped)

LT_BEGIN_AUTO_TEST(engine_fields_suite, close_delimited_appends_close)
    // HTTP/1.0, no CL: the body runs to EOF, so the verdict is close.
    http::fields f;
    f.append("Content-Type", "text/plain");
    const std::string out = emit_head(get_10(), 200, f);
    LT_CHECK(out == "HTTP/1.0 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Connection: close\r\n"
                    "\r\n");
LT_END_AUTO_TEST(close_delimited_appends_close)

LT_BEGIN_AUTO_TEST(engine_fields_suite, no_content_strips_cl_and_te)
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "5");
    f.append("Transfer-Encoding", "chunked");
    const std::string out = emit_head(get_11(), 204, f);
    LT_CHECK(out == "HTTP/1.1 204 No Content\r\n"
                    "Content-Type: text/plain\r\n"
                    "\r\n");
LT_END_AUTO_TEST(no_content_strips_cl_and_te)

LT_BEGIN_AUTO_TEST(engine_fields_suite, not_modified_keeps_cl_strips_te)
    http::fields f;
    f.append("Content-Length", "5");
    f.append("Transfer-Encoding", "chunked");
    const std::string out = emit_head(get_11(), 304, f);
    LT_CHECK(out == "HTTP/1.1 304 Not Modified\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n");
LT_END_AUTO_TEST(not_modified_keeps_cl_strips_te)

LT_BEGIN_AUTO_TEST(engine_fields_suite, head_response_is_verbatim)
    http::request_head req = request(http::method_id::head,
                                     http::protocol::http_1_1);
    http::fields f;
    f.append("Content-Length", "7");
    f.append("Transfer-Encoding", "chunked");
    const std::string out = emit_head(req, 200, f);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "Content-Length: 7\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n");
LT_END_AUTO_TEST(head_response_is_verbatim)

LT_BEGIN_AUTO_TEST(engine_fields_suite, engine_selected_te_appended_last)
    // HTTP/1.1 body response the handler framed with nothing: the
    // engine appends Transfer-Encoding: chunked as the framing field.
    http::fields f;
    f.append("Content-Type", "text/plain");
    const std::string out = emit_head(get_11(), 200, f, fixed_clock());
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n");
LT_END_AUTO_TEST(engine_selected_te_appended_last)

LT_BEGIN_AUTO_TEST(engine_fields_suite, handler_te_chunked_not_duplicated)
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Transfer-Encoding", "Chunked");
    const std::string out = emit_head(get_11(), 200, f);
    LT_CHECK(out == "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Transfer-Encoding: Chunked\r\n"
                    "\r\n");
LT_END_AUTO_TEST(handler_te_chunked_not_duplicated)

LT_BEGIN_AUTO_TEST(engine_fields_suite, upgrade_101_head_only)
    // The websocket corpus's 101 handshake commits as a final head-only
    // response: handler fields verbatim, Date engine field, no framing.
    http::fields f;
    f.append("Upgrade", "websocket");
    f.append("Sec-WebSocket-Accept", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    const std::string out = emit_head(get_11(), 101, f, fixed_clock());
    LT_CHECK(out == "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                    "\r\n");
LT_END_AUTO_TEST(upgrade_101_head_only)

LT_BEGIN_AUTO_TEST(engine_fields_suite, interim_head_byte_exact)
    http1_response_framer framer;
    std::string out;
    LT_CHECK(framer.interim_head(out, 100).ok());
    LT_CHECK(out == "HTTP/1.1 100 Continue\r\n\r\n");

    http1_response_framer other;
    std::string out2;
    LT_CHECK(other.interim_head(out2, 103).ok());
    LT_CHECK(out2 == "HTTP/1.1 103 Early Hints\r\n\r\n");
LT_END_AUTO_TEST(interim_head_byte_exact)

LT_BEGIN_AUTO_TEST(engine_fields_suite, interim_rejects_final_codes)
    http1_response_framer framer;
    std::string out;
    LT_CHECK(framer.interim_head(out, 200).code()
             == http::outcome_code::invalid_argument);
    LT_CHECK(framer.interim_head(out, 99).code()
             == http::outcome_code::invalid_argument);
    LT_CHECK(framer.interim_head(out, 0).code()
             == http::outcome_code::invalid_argument);
    LT_CHECK(out.empty());
LT_END_AUTO_TEST(interim_rejects_final_codes)

LT_BEGIN_AUTO_TEST(engine_fields_suite, icy_status_token_replaces_version)
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "2");
    const std::string out = emit_head(get_11(), 200, f, fixed_clock(),
                                      "ICY");
    LT_CHECK(out == "ICY 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
                    "Content-Length: 2\r\n"
                    "\r\n");
LT_END_AUTO_TEST(icy_status_token_replaces_version)

LT_BEGIN_AUTO_TEST(engine_fields_suite, fields_never_mutated_by_emission)
    // The strip rules apply to the emitted sequence only: the
    // committed fields object still carries everything.
    http::fields f;
    f.append("Connection", "close");
    f.append("Content-Length", "5");
    f.append("Transfer-Encoding", "chunked");
    http1_response_framer framer;
    std::string out;
    LT_CHECK(framer.start_head(out, get_11(),
                               http::status::from_code(204), f).ok());
    LT_CHECK_EQ(f.size(), 3u);
    LT_CHECK(f.first("connection").has_value());
    LT_CHECK(f.first("content-length").has_value());
    LT_CHECK(f.first("transfer-encoding").has_value());
LT_END_AUTO_TEST(fields_never_mutated_by_emission)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
