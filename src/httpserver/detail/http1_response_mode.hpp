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

// TASK-107: authoritative HTTP/1 response framing decision and
// keep-alive computation (PRD-V3N-REQ-004/026, DR-V3-006). NOT part of
// the installed surface.
//
// The response-side mirror of http1_body_mode.hpp: one decision,
// evaluated top to bottom over the committed status, the request head,
// and the handler's response field occurrences (RFC 9112 section 6.3).
// The engine computes the mode exactly once, at response-commit time
// (sink start()); the framer and the outbox consume it and never
// recompute. Every strictness delta vs the v2
// emission is pre-approved for v3 and recorded as a migration note at
// its site.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_response_mode.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_MODE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_MODE_HPP_

#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <string_view>
#include <system_error>

#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

namespace detail {

// The one authoritative framing of a response body. none: 1xx (incl.
// 101) or 204 — head-only, the engine adds no framing fields;
// metadata_only: 304 — the handler's fields pass through (a
// Content-Length may ride as metadata), no body bytes are emitted;
// head_no_body: the response to a HEAD request — fields verbatim, no
// framing fields added; length: exactly one valid handler-provided
// Content-Length; chunked: handler-pinned Transfer-Encoding: chunked,
// or engine-selected on HTTP/1.1 when the handler framed nothing;
// close_delimited: no CL and HTTP/1.0 — the body runs to EOF and the
// connection closes after it.
enum class http1_response_body_kind : std::uint8_t {
    none,
    metadata_only,
    head_no_body,
    length,
    chunked,
    close_delimited,
};

// Keep-alive verdict of one response. Emission policy (framer): close
// appends "Connection: close"; HTTP/1.0 keep-alive appends
// "Connection: keep-alive"; HTTP/1.1 keep-alive appends nothing.
enum class http1_keepalive : std::uint8_t { keep_alive, close };

struct http1_response_mode {
    http1_response_body_kind kind = http1_response_body_kind::none;
    std::uint64_t content_length = 0;  // length kind only
    http::outcome failure;             // rejected only
    http1_close_policy close_policy = http1_close_policy::none;

    // The authoritative decision for one committed response. Pure:
    // reads the inputs, returns the mode, mutates nothing. The status
    // must be a final (200..599) code through this path — informational
    // responses belong on the framer's interim path, with the 101
    // upgrade handshake as the one 1xx that legitimately commits as a
    // final head-only response.
    static http1_response_mode compute(const http::request_head& request,
                                       const http::status& s,
                                       const http::fields& fields);

 private:
    // The framing branch over a body-carrying final response: TE vs CL
    // reconciliation, then the version-based engine selection.
    static void compute_body_framing(http1_response_mode& mode,
                                     const http::request_head& request,
                                     const http::fields& fields);

    // Records a typed rejection; the mode's failure is sticky.
    static http1_response_mode& reject(http1_response_mode& mode,
                                       http::outcome_code code,
                                       std::string what,
                                       http1_close_policy policy);

    // The response side accepts exactly one final "chunked" element;
    // every other spelling is a protocol error. Migration note: the
    // request side answers an unsupported coding with 501 and keeps the
    // connection; a response cannot unsend its head, so the handler-
    // pinned coding list is all-or-nothing and closes.
    static void compute_from_te(http1_response_mode& mode,
                                const std::span<const std::string>& values);

    // Exactly one occurrence, whose whole comma-separated list is the
    // single token "chunked" (case-insensitive).
    static bool is_single_chunked_te(
        const std::span<const std::string>& values);

    // The response side mirrors the request-side Content-Length
    // grammar: exactly one occurrence, 1*DIGIT without a leading zero
    // and without overflow.
    static void compute_from_cl(http1_response_mode& mode,
                                const std::span<const std::string>& values);

    // 1*DIGIT, no leading zero, no overflow (from_chars backend).
    static bool parse_length(std::string_view value, std::uint64_t& out);
};

// True iff any Connection value's comma-separated token list contains
// `token` (case-insensitive, OWS-trimmed).
bool has_connection_token(const std::span<const std::string>& values,
                                 std::string_view token);

// The keep-alive verdict of one response (RFC 9112 section 9.3):
// close-delimited bodies always close; a request that announced close
// gets close; HTTP/1.0 defaults to close unless it announced
// keep-alive; otherwise keep-alive. A nonzero hint (the engine's
// respond_then_close / close_now posture) forces close.
http1_keepalive http1_response_keepalive(
    const http::request_head& request, http1_response_body_kind kind,
    http1_close_policy hint);

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_MODE_HPP_
