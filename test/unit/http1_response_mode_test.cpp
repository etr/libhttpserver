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

// TASK-107 step 1: authoritative HTTP/1 response framing decision tests.
// Pins detail::http1_response_mode::compute over (request head, response
// status, handler fields) rows of the RFC 9112 section 6.3 response-side
// table:
//   - the accepted rows (length, handler-pinned chunked, engine-selected
//     chunked, close-delimited, the no-body kinds) with the close policy
//     at none and the length carried exactly;
//   - the rejection rows, each with its exact typed outcome and close
//     posture (DR-V3-006): protocol_error and close_now for framing
//     ambiguity, invalid_argument and respond_then_close for a status
//     the response path must never see.
//
// TASK-107 step 2: keep-alive computation. Pins
// detail::http1_response_keepalive over the RFC 9112 section 9.3 matrix:
// close-delimited always closes, a request close token forces close,
// HTTP/1.0 defaults to close unless it announced keep-alive, and the
// engine's close hint overrides everything.

#include <cstdint>
#include <string>

#include <httpserver/detail/http1_response_mode.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;

using httpserver::detail::http1_close_policy;
using httpserver::detail::http1_keepalive;
using httpserver::detail::http1_response_body_kind;
using httpserver::detail::http1_response_mode;
using httpserver::http::outcome_code;

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

http::request_head head_11() {
    return request(http::method_id::head, http::protocol::http_1_1);
}

// True iff the triple yields the expected accepted mode: exact kind and
// content length, a clean failure, no close action.
bool accepts_as(const http::request_head& req, std::uint16_t code,
                const http::fields& fields, http1_response_body_kind kind,
                std::uint64_t content_length) {
    const http1_response_mode m = http1_response_mode::compute(
        req, http::status::from_code(code), fields);
    return m.kind == kind && m.content_length == content_length
        && m.close_policy == http1_close_policy::none && m.failure.ok();
}

// True iff the triple is rejected: the exact typed outcome with a
// diagnostic and the exact close posture.
bool rejects_as(const http::request_head& req, std::uint16_t code,
                const http::fields& fields, outcome_code expected,
                http1_close_policy policy) {
    const http1_response_mode m = http1_response_mode::compute(
        req, http::status::from_code(code), fields);
    return m.failure.code() == expected && !m.failure.message().empty()
        && m.close_policy == policy;
}

}  // namespace

LT_BEGIN_SUITE(response_mode_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(response_mode_suite)

LT_BEGIN_AUTO_TEST(response_mode_suite, default_mode_is_bodyless)
    const http1_response_mode m;
    LT_CHECK(m.kind == http1_response_body_kind::none);
    LT_CHECK_EQ(m.content_length, 0u);
    LT_CHECK(m.failure.ok());
    LT_CHECK(m.close_policy == http1_close_policy::none);
LT_END_AUTO_TEST(default_mode_is_bodyless)

LT_BEGIN_AUTO_TEST(response_mode_suite, length_from_handler_cl)
    http::fields f;
    f.append("Content-Type", "text/plain");
    f.append("Content-Length", "2");
    LT_CHECK(accepts_as(get_11(), 200, f,
                        http1_response_body_kind::length, 2));
LT_END_AUTO_TEST(length_from_handler_cl)

LT_BEGIN_AUTO_TEST(response_mode_suite, length_ignores_method_and_version)
    http::fields f;
    f.append("Content-Length", "5");
    LT_CHECK(accepts_as(get_10(), 200, f,
                        http1_response_body_kind::length, 5));
    LT_CHECK(accepts_as(get_11(), 404, f,
                        http1_response_body_kind::length, 5));
LT_END_AUTO_TEST(length_ignores_method_and_version)

LT_BEGIN_AUTO_TEST(response_mode_suite, handler_te_chunked_is_honored)
    http::fields f;
    f.append("Transfer-Encoding", "chunked");
    LT_CHECK(accepts_as(get_11(), 200, f,
                        http1_response_body_kind::chunked, 0));
LT_END_AUTO_TEST(handler_te_chunked_is_honored)

LT_BEGIN_AUTO_TEST(response_mode_suite, handler_te_chunked_case_insensitive)
    http::fields f;
    f.append("Transfer-Encoding", "Chunked");
    LT_CHECK(accepts_as(get_11(), 200, f,
                        http1_response_body_kind::chunked, 0));
    http::fields upper;
    upper.append("TRANSFER-ENCODING", "CHUNKED");
    LT_CHECK(accepts_as(get_11(), 200, upper,
                        http1_response_body_kind::chunked, 0));
LT_END_AUTO_TEST(handler_te_chunked_case_insensitive)

LT_BEGIN_AUTO_TEST(response_mode_suite, te_chunked_on_http_10_is_honored)
    // Response side: the request version gates engine selection, not the
    // handler-pinned coding; a handler that pins chunked gets chunked.
    http::fields f;
    f.append("Transfer-Encoding", "chunked");
    LT_CHECK(accepts_as(get_10(), 200, f,
                        http1_response_body_kind::chunked, 0));
LT_END_AUTO_TEST(te_chunked_on_http_10_is_honored)

LT_BEGIN_AUTO_TEST(response_mode_suite, te_and_cl_is_rejected)
    http::fields te_first;
    te_first.append("Transfer-Encoding", "chunked");
    te_first.append("Content-Length", "5");
    LT_CHECK(rejects_as(get_11(), 200, te_first,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
    http::fields cl_first;
    cl_first.append("Content-Length", "5");
    cl_first.append("Transfer-Encoding", "chunked");
    LT_CHECK(rejects_as(get_11(), 200, cl_first,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
LT_END_AUTO_TEST(te_and_cl_is_rejected)

LT_BEGIN_AUTO_TEST(response_mode_suite, double_cl_is_rejected)
    http::fields identical;
    identical.append("Content-Length", "5");
    identical.append("Content-Length", "5");
    LT_CHECK(rejects_as(get_11(), 200, identical,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
    http::fields differing;
    differing.append("Content-Length", "5");
    differing.append("Content-Length", "6");
    LT_CHECK(rejects_as(get_11(), 200, differing,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
LT_END_AUTO_TEST(double_cl_is_rejected)

LT_BEGIN_AUTO_TEST(response_mode_suite, invalid_cl_grammar_is_rejected)
    const char* const bad[] = {
        "5a",
        "a5",
        "",
        "007",
        "5 6",
        "-1",
    };
    for (const char* const value : bad) {
        http::fields f;
        f.append("Content-Length", value);
        LT_CHECK(rejects_as(get_11(), 200, f,
                            outcome_code::protocol_error,
                            http1_close_policy::close_now));
    }
LT_END_AUTO_TEST(invalid_cl_grammar_is_rejected)

LT_BEGIN_AUTO_TEST(response_mode_suite, te_identity_is_rejected)
    http::fields f;
    f.append("Transfer-Encoding", "identity");
    LT_CHECK(rejects_as(get_11(), 200, f,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
LT_END_AUTO_TEST(te_identity_is_rejected)

LT_BEGIN_AUTO_TEST(response_mode_suite, te_chunked_twice_is_rejected)
    http::fields one_line;
    one_line.append("Transfer-Encoding", "chunked, chunked");
    LT_CHECK(rejects_as(get_11(), 200, one_line,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
    http::fields two_lines;
    two_lines.append("Transfer-Encoding", "chunked");
    two_lines.append("Transfer-Encoding", "chunked");
    LT_CHECK(rejects_as(get_11(), 200, two_lines,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
LT_END_AUTO_TEST(te_chunked_twice_is_rejected)

LT_BEGIN_AUTO_TEST(response_mode_suite, te_coding_before_chunked_rejected)
    http::fields f;
    f.append("Transfer-Encoding", "gzip, chunked");
    LT_CHECK(rejects_as(get_11(), 200, f,
                        outcome_code::protocol_error,
                        http1_close_policy::close_now));
LT_END_AUTO_TEST(te_coding_before_chunked_rejected)

LT_BEGIN_AUTO_TEST(response_mode_suite, engine_selected_chunked_on_11)
    http::fields f;
    f.append("Content-Type", "text/plain");
    LT_CHECK(accepts_as(get_11(), 200, f,
                        http1_response_body_kind::chunked, 0));
LT_END_AUTO_TEST(engine_selected_chunked_on_11)

LT_BEGIN_AUTO_TEST(response_mode_suite, close_delimited_on_10)
    http::fields f;
    f.append("Content-Type", "text/plain");
    LT_CHECK(accepts_as(get_10(), 200, f,
                        http1_response_body_kind::close_delimited, 0));
LT_END_AUTO_TEST(close_delimited_on_10)

LT_BEGIN_AUTO_TEST(response_mode_suite, no_content_strips_framing_fields)
    // 204: kind none; the handler CL/TE occurrences (stripped at
    // emission, step 4) do not make the mode a body mode.
    http::fields with_cl;
    with_cl.append("Content-Length", "5");
    LT_CHECK(accepts_as(get_11(), 204, with_cl,
                        http1_response_body_kind::none, 0));
    http::fields with_te;
    with_te.append("Transfer-Encoding", "chunked");
    LT_CHECK(accepts_as(get_11(), 204, with_te,
                        http1_response_body_kind::none, 0));
LT_END_AUTO_TEST(no_content_strips_framing_fields)

LT_BEGIN_AUTO_TEST(response_mode_suite, informational_statuses_have_no_body)
    // 1xx through the final path stays head-only (the 101 upgrade handshake
    // is committed exactly this way); framing fields are never added.
    for (std::uint16_t code : {100u, 101u, 150u, 199u}) {
        http::fields f;
        f.append("Content-Length", "5");
        LT_CHECK(accepts_as(get_11(), code, f,
                            http1_response_body_kind::none, 0));
    }
LT_END_AUTO_TEST(informational_statuses_have_no_body)

LT_BEGIN_AUTO_TEST(response_mode_suite, not_modified_is_metadata_only)
    http::fields with_cl;
    with_cl.append("Content-Length", "5");
    LT_CHECK(accepts_as(get_11(), 304, with_cl,
                        http1_response_body_kind::metadata_only, 0));
    http::fields with_te;
    with_te.append("Transfer-Encoding", "chunked");
    LT_CHECK(accepts_as(get_11(), 304, with_te,
                        http1_response_body_kind::metadata_only, 0));
LT_END_AUTO_TEST(not_modified_is_metadata_only)

LT_BEGIN_AUTO_TEST(response_mode_suite, head_requests_pass_fields_through)
    // HEAD: fields verbatim, no framing validation, no framing fields.
    http::fields with_cl;
    with_cl.append("Content-Length", "7");
    LT_CHECK(accepts_as(head_11(), 200, with_cl,
                        http1_response_body_kind::head_no_body, 0));
    http::fields bare;
    bare.append("Content-Type", "text/plain");
    LT_CHECK(accepts_as(head_11(), 200, bare,
                        http1_response_body_kind::head_no_body, 0));
    // Even a TE+CL combination stays verbatim on HEAD (documented v3
    // strictness delta: the HEAD emission carries no body to frame).
    http::fields both;
    both.append("Content-Length", "7");
    both.append("Transfer-Encoding", "chunked");
    LT_CHECK(accepts_as(head_11(), 200, both,
                        http1_response_body_kind::head_no_body, 0));
LT_END_AUTO_TEST(head_requests_pass_fields_through)

LT_BEGIN_AUTO_TEST(response_mode_suite, head_applies_to_any_version)
    http::fields f;
    f.append("Content-Length", "7");
    LT_CHECK(accepts_as(request(http::method_id::head,
                                http::protocol::http_1_0), 200, f,
                        http1_response_body_kind::head_no_body, 0));
LT_END_AUTO_TEST(head_applies_to_any_version)

LT_BEGIN_AUTO_TEST(response_mode_suite, invalid_status_is_rejected)
    for (std::uint16_t code : {0u, 99u, 600u, 1000u}) {
        http::fields f;
        f.append("Content-Length", "5");
        LT_CHECK(rejects_as(get_11(), code, f,
                            outcome_code::invalid_argument,
                            http1_close_policy::respond_then_close));
    }
LT_END_AUTO_TEST(invalid_status_is_rejected)

LT_BEGIN_AUTO_TEST(response_mode_suite, rejection_carries_engine_prefix)
    http::fields f;
    f.append("Content-Length", "5a");
    const http1_response_mode m = http1_response_mode::compute(
        get_11(), http::status::from_code(200), f);
    LT_CHECK(m.failure.message().find("http1_response_mode")
             != std::string::npos);
LT_END_AUTO_TEST(rejection_carries_engine_prefix)

LT_BEGIN_SUITE(keepalive_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(keepalive_suite)

// True iff the keep-alive verdict for (request, kind, hint) is `want`.
bool keepalive_is(const http::request_head& req,
                  http1_response_body_kind kind, http1_close_policy hint,
                  http1_keepalive want) {
    return httpserver::detail::http1_response_keepalive(req, kind, hint)
        == want;
}

LT_BEGIN_AUTO_TEST(keepalive_suite, http_11_defaults_to_keep_alive)
    LT_CHECK(keepalive_is(get_11(), http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::keep_alive));
    LT_CHECK(keepalive_is(get_11(), http1_response_body_kind::chunked,
                          http1_close_policy::none,
                          http1_keepalive::keep_alive));
    LT_CHECK(keepalive_is(get_11(), http1_response_body_kind::none,
                          http1_close_policy::none,
                          http1_keepalive::keep_alive));
LT_END_AUTO_TEST(http_11_defaults_to_keep_alive)

LT_BEGIN_AUTO_TEST(keepalive_suite, request_close_token_forces_close)
    http::request_head req = get_11();
    req.head_fields.append("Connection", "close");
    LT_CHECK(keepalive_is(req, http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::close));
LT_END_AUTO_TEST(request_close_token_forces_close)

LT_BEGIN_AUTO_TEST(keepalive_suite, close_token_scan_is_case_insensitive)
    http::request_head req = get_11();
    req.head_fields.append("Connection", "Close");
    LT_CHECK(keepalive_is(req, http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::close));
    http::request_head list = get_11();
    list.head_fields.append("Connection", "keep-alive, close");
    LT_CHECK(keepalive_is(list, http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::close));
LT_END_AUTO_TEST(close_token_scan_is_case_insensitive)

LT_BEGIN_AUTO_TEST(keepalive_suite, close_token_scan_is_whole_token)
    // Substring proximity is not a token match.
    http::request_head req = get_11();
    req.head_fields.append("Connection", "keep-alive, closeness");
    LT_CHECK(keepalive_is(req, http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::keep_alive));
LT_END_AUTO_TEST(close_token_scan_is_whole_token)

LT_BEGIN_AUTO_TEST(keepalive_suite, close_token_scan_spans_occurrences)
    http::request_head req = get_11();
    req.head_fields.append("Connection", "keep-alive");
    req.head_fields.append("Connection", "close");
    LT_CHECK(keepalive_is(req, http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::close));
LT_END_AUTO_TEST(close_token_scan_spans_occurrences)

LT_BEGIN_AUTO_TEST(keepalive_suite, http_10_defaults_to_close)
    LT_CHECK(keepalive_is(get_10(), http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::close));
    LT_CHECK(keepalive_is(get_10(), http1_response_body_kind::chunked,
                          http1_close_policy::none,
                          http1_keepalive::close));
LT_END_AUTO_TEST(http_10_defaults_to_close)

LT_BEGIN_AUTO_TEST(keepalive_suite, http_10_keepalive_token_keeps_alive)
    http::request_head req = get_10();
    req.head_fields.append("Connection", "keep-alive");
    LT_CHECK(keepalive_is(req, http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::keep_alive));
    http::request_head mixed = get_10();
    mixed.head_fields.append("Connection", "Keep-Alive");
    LT_CHECK(keepalive_is(mixed, http1_response_body_kind::length,
                          http1_close_policy::none,
                          http1_keepalive::keep_alive));
LT_END_AUTO_TEST(http_10_keepalive_token_keeps_alive)

LT_BEGIN_AUTO_TEST(keepalive_suite, close_delimited_always_closes)
    // Even an HTTP/1.0 keep-alive announcement cannot keep a
    // close-delimited body's connection open: EOF is the terminator.
    http::request_head req = get_10();
    req.head_fields.append("Connection", "keep-alive");
    LT_CHECK(keepalive_is(req, http1_response_body_kind::close_delimited,
                          http1_close_policy::none,
                          http1_keepalive::close));
LT_END_AUTO_TEST(close_delimited_always_closes)

LT_BEGIN_AUTO_TEST(keepalive_suite, engine_hint_forces_close)
    http::request_head keep_10 = get_10();
    keep_10.head_fields.append("Connection", "keep-alive");
    LT_CHECK(keepalive_is(keep_10, http1_response_body_kind::length,
                          http1_close_policy::respond_then_close,
                          http1_keepalive::close));
    LT_CHECK(keepalive_is(get_11(), http1_response_body_kind::length,
                          http1_close_policy::close_now,
                          http1_keepalive::close));
LT_END_AUTO_TEST(engine_hint_forces_close)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
