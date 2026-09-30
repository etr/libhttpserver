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
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>
#include <parity/normalize.hpp>
#include <parity/response_frame.hpp>
#include <parity/transcript.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;

using httpserver::detail::http1_keepalive;
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
    std::vector<observed_response> done = parser.feed(head);
    for (observed_response& r : parser.feed(body)) {
        done.push_back(std::move(r));
    }
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

LT_BEGIN_SUITE(body_framing_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(body_framing_suite)

// The maximum output bound the standalone (outbox-less) tests use.
constexpr std::size_t unlimited = static_cast<std::size_t>(-1);

std::span<const std::byte> as_bytes(const std::string& s) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size());
}

http::fields length_fields(const std::string& body) {
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", std::to_string(body.size()));
    return f;
}

LT_BEGIN_AUTO_TEST(body_framing_suite, length_body_is_raw_and_exact)
    const std::string body = "Not Found";
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(),
                               http::status::from_code(404),
                               length_fields(body)).ok());
    LT_CHECK(framer.push_body(wire, as_bytes(body), unlimited).ok());
    http::fields no_trailers;
    LT_CHECK(framer.finish_body(wire, no_trailers).ok());
    const observed_response r = parse_back(wire, "");
    LT_CHECK(r.status == 404);
    LT_CHECK(r.body == "Not Found");
    LT_CHECK(r.framing == "content-length");
LT_END_AUTO_TEST(length_body_is_raw_and_exact)

LT_BEGIN_AUTO_TEST(body_framing_suite, length_push_splits_across_calls)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(),
                               http::status::from_code(200),
                               length_fields("abcdef")).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("abc"), unlimited).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("def"), unlimited).ok());
    http::fields no_trailers;
    LT_CHECK(framer.finish_body(wire, no_trailers).ok());
    LT_CHECK(parse_back(wire, "").body == "abcdef");
LT_END_AUTO_TEST(length_push_splits_across_calls)

LT_BEGIN_AUTO_TEST(body_framing_suite, length_overrun_fails_sticky)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(),
                               http::status::from_code(200),
                               length_fields("abc")).ok());
    const http::outcome over = framer.push_body(wire, as_bytes("abcd"),
                                                unlimited);
    LT_CHECK(over.code() == http::outcome_code::protocol_error);
    LT_CHECK(framer.failed());
    // Sticky: nothing further passes.
    const http::outcome again = framer.push_body(wire, as_bytes("a"),
                                                 unlimited);
    LT_CHECK(again.code() == http::outcome_code::protocol_error);
    http::fields no_trailers;
    LT_CHECK(framer.finish_body(wire, no_trailers).code()
             == http::outcome_code::protocol_error);
LT_END_AUTO_TEST(length_overrun_fails_sticky)

LT_BEGIN_AUTO_TEST(body_framing_suite, length_underrun_finish_fails)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(),
                               http::status::from_code(200),
                               length_fields("abc")).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("ab"), unlimited).ok());
    http::fields no_trailers;
    const http::outcome early = framer.finish_body(wire, no_trailers);
    LT_CHECK(early.code() == http::outcome_code::protocol_error);
    LT_CHECK(framer.failed());
LT_END_AUTO_TEST(length_underrun_finish_fails)

LT_BEGIN_AUTO_TEST(body_framing_suite, length_respects_max_out_bytes)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(),
                               http::status::from_code(200),
                               length_fields("abcdef")).ok());
    const std::size_t head_bytes = wire.size();
    LT_CHECK(framer.push_body(wire, as_bytes("abcdef"), 4).ok());
    LT_CHECK_EQ(wire.size() - head_bytes, 4u);  // affordable prefix only
    LT_CHECK(framer.push_body(wire, as_bytes("ef"), unlimited).ok());
    LT_CHECK(parse_back(wire, "").body == "abcdef");
LT_END_AUTO_TEST(length_respects_max_out_bytes)

LT_BEGIN_AUTO_TEST(body_framing_suite, chunked_encode_is_lowercase_exact)
    // The plan's pinned example, split pushes and all.
    http::fields handler;
    handler.append("Content-Type", "text/plain");
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(),
                               http::status::from_code(200),
                               handler).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("Wiki"), unlimited).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("pedia"), unlimited).ok());
    http::fields trailers;
    trailers.append("X-Total", "9");
    LT_CHECK(framer.finish_body(wire, trailers).ok());
    const std::string expected_head =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";
    LT_CHECK(wire == expected_head
             + "4\r\nWiki\r\n5\r\npedia\r\n0\r\nX-Total: 9\r\n\r\n");
    const observed_response r = parse_back(wire, "");
    LT_CHECK(r.framing == "chunked");
    LT_CHECK(r.body == "Wikipedia");
    LT_CHECK(r.headers.size() == 3);
    LT_CHECK(r.headers[2].name == "X-Total");
    LT_CHECK(r.headers[2].value == "9");
LT_END_AUTO_TEST(chunked_encode_is_lowercase_exact)

LT_BEGIN_AUTO_TEST(body_framing_suite, chunked_zero_pushes_emit_terminator)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(), http::status::from_code(200),
                               http::fields()).ok());
    http::fields no_trailers;
    LT_CHECK(framer.finish_body(wire, no_trailers).ok());
    LT_CHECK(wire == "HTTP/1.1 200 OK\r\n"
                     "Transfer-Encoding: chunked\r\n"
                     "\r\n"
                     "0\r\n\r\n");
LT_END_AUTO_TEST(chunked_zero_pushes_emit_terminator)

LT_BEGIN_AUTO_TEST(body_framing_suite, chunked_multiple_trailers_in_order)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(), http::status::from_code(200),
                               http::fields()).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("x"), unlimited).ok());
    http::fields trailers;
    trailers.append("X-B", "2");
    trailers.append("X-A", "1");
    LT_CHECK(framer.finish_body(wire, trailers).ok());
    LT_CHECK(wire.find("1\r\nx\r\n0\r\nX-B: 2\r\nX-A: 1\r\n\r\n")
             != std::string::npos);
LT_END_AUTO_TEST(chunked_multiple_trailers_in_order)

LT_BEGIN_AUTO_TEST(body_framing_suite, chunked_split_by_max_out_bytes)
    // A push that cannot fully fit encodes the affordable prefix as its
    // own chunk and reports nothing lost; the caller re-pushes the rest.
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(), http::status::from_code(200),
                               http::fields()).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("abcdef"), 8).ok());
    // 8 bytes of output budget: hex width + 2 CRLFs + payload must fit,
    // so the chunk carries the 3-byte prefix.
    LT_CHECK(wire.find("3\r\nabc\r\n") != std::string::npos);
    // The push consumed the 3-byte prefix; the caller re-pushes the
    // unconsumed remainder.
    LT_CHECK(framer.push_body(wire, as_bytes("def"), unlimited).ok());
    http::fields no_trailers;
    LT_CHECK(framer.finish_body(wire, no_trailers).ok());
    LT_CHECK(parse_back(wire, "").body == "abcdef");
LT_END_AUTO_TEST(chunked_split_by_max_out_bytes)

LT_BEGIN_AUTO_TEST(body_framing_suite, trailers_under_length_fail_at_finish)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(),
                               http::status::from_code(200),
                               length_fields("abc")).ok());
    LT_CHECK(framer.push_body(wire, as_bytes("abc"), unlimited).ok());
    http::fields trailers;
    trailers.append("X-Total", "3");
    LT_CHECK(framer.finish_body(wire, trailers).code()
             == http::outcome_code::protocol_error);
    LT_CHECK(framer.failed());
LT_END_AUTO_TEST(trailers_under_length_fail_at_finish)

LT_BEGIN_AUTO_TEST(body_framing_suite, body_push_on_no_body_kinds_fails)
    // close_delimited: strictly no push (the v3 engine frames close-
    // delimited bodies only through the connection loop's EOF path).
    http::fields handler;
    handler.append("Content-Type", "text/plain");
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_10(),
                               http::status::from_code(200),
                               handler).ok());
    const http::outcome pushed = framer.push_body(wire, as_bytes("x"),
                                                  unlimited);
    LT_CHECK(pushed.code() == http::outcome_code::protocol_error);

    // none (204) and head_no_body reject body bytes the same way.
    http1_response_framer none_framer;
    std::string none_wire;
    LT_CHECK(none_framer.start_head(none_wire, get_11(),
                                    http::status::from_code(204),
                                    handler).ok());
    LT_CHECK(none_framer.push_body(none_wire, as_bytes("x"), unlimited)
                 .code() == http::outcome_code::protocol_error);

    http::request_head head_req = request(http::method_id::head,
                                          http::protocol::http_1_1);
    http1_response_framer head_framer;
    std::string head_wire;
    LT_CHECK(head_framer.start_head(head_wire, head_req,
                                    http::status::from_code(200),
                                    length_fields("abc")).ok());
    LT_CHECK(head_framer.push_body(head_wire, as_bytes("abc"), unlimited)
                 .code() == http::outcome_code::protocol_error);
LT_END_AUTO_TEST(body_push_on_no_body_kinds_fails)

LT_BEGIN_AUTO_TEST(body_framing_suite, finish_twice_fails_typed)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.start_head(wire, get_11(), http::status::from_code(200),
                               length_fields("")).ok());
    http::fields no_trailers;
    LT_CHECK(framer.finish_body(wire, no_trailers).ok());
    LT_CHECK(framer.finish_body(wire, no_trailers).code()
             == http::outcome_code::invalid_state);
    LT_CHECK(framer.push_body(wire, as_bytes("x"), unlimited).code()
             == http::outcome_code::invalid_state);
LT_END_AUTO_TEST(finish_twice_fails_typed)

LT_BEGIN_AUTO_TEST(body_framing_suite, push_before_head_fails_typed)
    http1_response_framer framer;
    std::string wire;
    LT_CHECK(framer.push_body(wire, as_bytes("x"), unlimited).code()
             == http::outcome_code::invalid_state);
    http::fields no_trailers;
    LT_CHECK(framer.finish_body(wire, no_trailers).code()
             == http::outcome_code::invalid_state);
    LT_CHECK(wire.empty());
LT_END_AUTO_TEST(push_before_head_fails_typed)

LT_BEGIN_SUITE(corpus_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(corpus_suite)

// Transcript-corpus replay (plan section 6). Each replayed case is the
// live corpus case, loaded at runtime from PARITY_TRANSCRIPT_DIR; the
// test synthesizes the request head plus the response inputs the case's
// v2 profile would have produced (documented per case), drives them
// through the real framer, parses the wire with the parity response
// parser, and asserts every response-scoped expectation through the
// corpus's own assertion engine (check_expectation under the
// Nth-status cursor rule, exactly as transcript_runner does).
//
// Replayed subset: routing.tseq (get_hello, parameterized_path,
// head_both_methods, method_not_allowed, not_found), forms.tseq
// (urlencoded_echo), file_resp.tseq (file_ok, file_missing), hooks.tseq
// (get_hello_hook_header, before_handler_403), shoutcast.tseq
// (icy_status_line, plain_get_still_http), websocket.tseq
// (upgrade_handshake_fixed_key), auth_basic.tseq (no_credentials; the
// plan's unauthorized_basic).
//
// Out of scope here (named per plan section 6): auth_digest (TASK-110),
// tls and ip_controls (TASK-108/114), route-adapter cases (TASK-110);
// pipelined_keepalive is replayed by the outbox suite.
//
// `expect connection X` is runner-level in the corpus: it resolves
// against the analogous http1_response_keepalive verdict instead. The
// one exception is the upgrade case, whose close is the engine's
// upgrade hand-off (the ws session owns the connection; the HTTP loop
// never continues), pinned directly.

struct replay_input {
    http::request_head request;
    std::uint16_t status = 200;
    http::fields fields;
    std::string body;
    std::string status_token;  // empty: mirrored request version
    bool head_only = false;    // parity parser HEAD hint
    bool upgraded = false;     // engine handed the connection to ws
};

http::request_head make_request(http::method_id id, http::protocol version,
                                const std::string& target,
                                std::initializer_list<
                                    std::pair<std::string_view,
                                              std::string_view>> client) {
    http::request_head head;
    head.raw_target = target;
    head.route_path = target;
    head.request_method = http::method::known(id);
    head.request_protocol = version;
    for (const auto& [name, value] : client) {
        head.head_fields.append(name, value);
    }
    return head;
}

http::fields make_fields(
    std::initializer_list<std::pair<std::string_view, std::string_view>> handler) {
    http::fields f;
    for (const auto& [name, value] : handler) {
        f.append(name, value);
    }
    return f;
}

// Drives one case through the framer and checks it against the corpus.
std::string replay_case(const char* file, const char* case_name,
                        const replay_input& in) {
    const parity::transcript t = parity::parse_transcript_file(
        std::string(PARITY_TRANSCRIPT_DIR "/") + file);
    const parity::tcase* found = nullptr;
    for (const parity::tcase& c : t.cases) {
        if (c.name == case_name) found = &c;
    }
    if (found == nullptr) {
        return "case not found: " + std::string(case_name);
    }

    http1_response_framer framer({}, in.status_token);
    std::string wire;
    const http::outcome head = framer.start_head(
        wire, in.request, http::status::from_code(in.status), in.fields);
    if (!head.ok()) return "start_head: " + head.message();
    if (!in.body.empty()) {
        const http::outcome pushed =
            framer.push_body(wire, as_bytes(in.body), unlimited);
        if (!pushed.ok()) return "push_body: " + pushed.message();
    }
    const http::outcome ended = framer.finish_body(wire, http::fields());
    if (!ended.ok()) return "finish_body: " + ended.message();

    parity::response_frame_parser parser;
    parser.set_head_only(in.head_only);
    std::vector<parity::observed_response> responses = parser.feed(wire);
    for (parity::observed_response& r : parser.finish()) {
        responses.push_back(std::move(r));
    }
    if (parser.failed()) return "parser: " + parser.error();
    if (responses.size() != 1) {
        return "expected one parsed response, got "
            + std::to_string(responses.size());
    }
    const std::vector<parity::normalized_exchange> exchanges{
        parity::normalize(responses.front())};

    // The connection verdict the engine would act on.
    const http1_response_mode mode = http1_response_mode::compute(
        in.request, http::status::from_code(in.status), in.fields);
    http1_keepalive verdict = httpserver::detail::http1_response_keepalive(
        in.request, mode.kind, mode.close_policy);
    if (in.upgraded) verdict = http1_keepalive::close;

    std::size_t index = 0;
    bool first = true;
    for (const parity::expectation& e : found->expects) {
        const bool advances = e.kind == parity::expect_kind::status
            || e.kind == parity::expect_kind::status_line;
        if (advances && !first) ++index;
        if (advances) first = false;
        if (e.kind == parity::expect_kind::connection) {
            const bool want_keep = e.value != "close";
            const bool got_keep = verdict == http1_keepalive::keep_alive;
            if (want_keep != got_keep) {
                return "expect-line=" + std::to_string(e.line)
                    + ": connection " + e.value
                    + " vs keepalive verdict";
            }
            continue;
        }
        if (e.kind == parity::expect_kind::closer) continue;
        if (index >= exchanges.size()) return "cursor past observed";
        const parity::match_result m = parity::check_expectation(
            e, exchanges[index], PARITY_TRANSCRIPT_DIR);
        if (!m.ok) return m.diff;
    }
    return "";
}

// Checks one replay: prints the corpus diff for the failure log and
// asserts it empty (the helper is inline: LT_CHECK needs the test
// frame).
void expect_replay(const std::string& diff, const char* what) {
    if (!diff.empty()) {
        std::cerr << "[replay " << what << "] " << diff << "\n";
    }
}

LT_BEGIN_AUTO_TEST(corpus_suite, routing_get_hello)
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/hello",
                              {{"Host", "127.0.0.1"}});
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "2"}});
    in.body = "OK";
    const std::string diff = replay_case("routing.tseq", "get_hello", in);
    expect_replay(diff, "get_hello");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(routing_get_hello)

LT_BEGIN_AUTO_TEST(corpus_suite, routing_parameterized_path)
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1,
                              "/params/42/name/jane",
                              {{"Host", "127.0.0.1"}});
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "15"}});
    in.body = "id=42;name=jane";
    const std::string diff = replay_case("routing.tseq", "parameterized_path", in);
    expect_replay(diff, "parameterized_path");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(routing_parameterized_path)

LT_BEGIN_AUTO_TEST(corpus_suite, routing_head_both_methods)
    // v2 answers HEAD headers-only while still declaring the GET body
    // length (framing none, body_len 0 on the wire); the parity parser
    // gets the HEAD hint.
    replay_input in;
    in.request = make_request(http::method_id::head,
                              http::protocol::http_1_1, "/both",
                              {{"Host", "127.0.0.1"}});
    in.fields = make_fields({{"Content-Length", "7"}});
    in.head_only = true;
    const std::string diff = replay_case("routing.tseq", "head_both_methods", in);
    expect_replay(diff, "head_both_methods");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(routing_head_both_methods)

LT_BEGIN_AUTO_TEST(corpus_suite, routing_method_not_allowed)
    replay_input in;
    in.request = make_request(http::method_id::post,
                              http::protocol::http_1_1, "/get_only",
                              {{"Host", "127.0.0.1"},
                               {"Content-Length", "0"}});
    in.status = 405;
    in.fields = make_fields({{"Allow", "GET"},
                             {"Content-Type", "text/plain"},
                             {"Content-Length", "18"}});
    in.body = "Method not Allowed";
    const std::string diff = replay_case("routing.tseq", "method_not_allowed", in);
    expect_replay(diff, "method_not_allowed");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(routing_method_not_allowed)

LT_BEGIN_AUTO_TEST(corpus_suite, routing_not_found)
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1,
                              "/definitely/not/there",
                              {{"Host", "127.0.0.1"}});
    in.status = 404;
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "9"}});
    in.body = "Not Found";
    const std::string diff = replay_case("routing.tseq", "not_found", in);
    expect_replay(diff, "not_found");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(routing_not_found)

LT_BEGIN_AUTO_TEST(corpus_suite, forms_urlencoded_echo)
    replay_input in;
    in.request = make_request(http::method_id::post,
                              http::protocol::http_1_1, "/echo_form",
                              {{"Host", "127.0.0.1"},
                               {"Content-Type",
                                "application/x-www-form-urlencoded"},
                               {"Content-Length", "9"}});
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "9"}});
    in.body = "a=1;b=two";
    const std::string diff = replay_case("forms.tseq", "urlencoded_echo", in);
    expect_replay(diff, "urlencoded_echo");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(forms_urlencoded_echo)

LT_BEGIN_AUTO_TEST(corpus_suite, file_resp_file_ok)
    // The wire shape comes from the owned bytes; the file source itself
    // is TASK-111/112.
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/file",
                              {{"Host", "127.0.0.1"}});
    in.fields = make_fields({{"Content-Type", "application/octet-stream"},
                             {"Content-Length", "21"}});
    in.body = "test content of file\n";
    const std::string diff = replay_case("file_resp.tseq", "file_ok", in);
    expect_replay(diff, "file_ok");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(file_resp_file_ok)

LT_BEGIN_AUTO_TEST(corpus_suite, file_resp_file_missing)
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/missing",
                              {{"Host", "127.0.0.1"}});
    in.status = 500;
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "21"}});
    in.body = "Internal Server Error";
    const std::string diff = replay_case("file_resp.tseq", "file_missing", in);
    expect_replay(diff, "file_missing");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(file_resp_file_missing)

LT_BEGIN_AUTO_TEST(corpus_suite, hooks_get_hello_hook_header)
    // The after_handler hook stamps X-Hook before the response fields;
    // the emitted order keeps the pinned X-Hook / Content-Type sequence.
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/hello",
                              {{"Host", "127.0.0.1"}});
    in.fields = make_fields({{"X-Hook", "after"},
                             {"Content-Type", "text/plain"},
                             {"Content-Length", "2"}});
    in.body = "OK";
    const std::string diff = replay_case("hooks.tseq", "get_hello_hook_header", in);
    expect_replay(diff, "get_hello_hook_header");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(hooks_get_hello_hook_header)

LT_BEGIN_AUTO_TEST(corpus_suite, hooks_before_handler_403)
    replay_input in;
    in.request = make_request(http::method_id::del,
                              http::protocol::http_1_1, "/admin",
                              {{"Host", "127.0.0.1"},
                               {"Content-Length", "0"}});
    in.status = 403;
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "9"}});
    in.body = "hooked403";
    const std::string diff = replay_case("hooks.tseq", "before_handler_403", in);
    expect_replay(diff, "before_handler_403");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(hooks_before_handler_403)

LT_BEGIN_AUTO_TEST(corpus_suite, shoutcast_icy_status_line)
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/stream",
                              {{"Host", "127.0.0.1"}});
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "2"}});
    in.body = "OK";
    in.status_token = "ICY";
    const std::string diff = replay_case("shoutcast.tseq", "icy_status_line", in);
    expect_replay(diff, "icy_status_line");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(shoutcast_icy_status_line)

LT_BEGIN_AUTO_TEST(corpus_suite, shoutcast_plain_get_still_http)
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/__smoke",
                              {{"Host", "127.0.0.1"}});
    in.fields = make_fields({{"Content-Type", "text/plain"},
                             {"Content-Length", "8"}});
    in.body = "smoke-ok";
    const std::string diff = replay_case("shoutcast.tseq", "plain_get_still_http", in);
    expect_replay(diff, "plain_get_still_http");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(shoutcast_plain_get_still_http)

LT_BEGIN_AUTO_TEST(corpus_suite, websocket_upgrade_handshake_fixed_key)
    // The engine's ws hand-off closes the HTTP loop after 101; the
    // replay pins the corpus connection expectation directly.
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/ws",
                              {{"Host", "127.0.0.1"},
                               {"Upgrade", "websocket"},
                               {"Connection", "Upgrade"},
                               {"Sec-WebSocket-Key",
                                "dGhlIHNhbXBsZSBub25jZQ=="},
                               {"Sec-WebSocket-Version", "13"}});
    in.status = 101;
    in.fields = make_fields({{"Upgrade", "websocket"},
                             {"Sec-WebSocket-Accept",
                              "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="}});
    in.upgraded = true;
    const std::string diff = replay_case("websocket.tseq", "upgrade_handshake_fixed_key", in);
    expect_replay(diff, "upgrade_handshake_fixed_key");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(websocket_upgrade_handshake_fixed_key)

LT_BEGIN_AUTO_TEST(corpus_suite, auth_basic_no_credentials)
    // auth_basic.tseq's challenge row (the plan's unauthorized_basic).
    replay_input in;
    in.request = make_request(http::method_id::get,
                              http::protocol::http_1_1, "/secret",
                              {{"Host", "127.0.0.1"}});
    in.status = 401;
    in.fields = make_fields({{"WWW-Authenticate",
                              "Basic realm=\"transcript\""},
                             {"Content-Length", "0"}});
    const std::string diff = replay_case("auth_basic.tseq", "no_credentials", in);
    expect_replay(diff, "no_credentials");
    LT_CHECK(diff.empty());
LT_END_AUTO_TEST(auth_basic_no_credentials)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
