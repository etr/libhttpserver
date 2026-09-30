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

// TASK-107: authoritative HTTP/1 response framing decision
// (PRD-V3N-REQ-004/026, DR-V3-006). NOT part of the installed surface.
//
// The response-side mirror of http1_body_mode.hpp: one decision,
// evaluated top to bottom over the committed status, the request head,
// and the handler's response field occurrences (RFC 9112 section 6.3).
// The engine computes the mode exactly once, at response-commit time
// (sink start()); the framer and the outbox consume it and never
// recompute. Every strictness delta vs the v2 (libmicrohttpd-backed)
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
                                       const http::fields& fields) {
        http1_response_mode mode;
        if (!s.valid()) {
            // Engine-internal misuse (an invalid status must never reach
            // a response commit); respond_then_close lets the engine
            // emit a synthesized error before the reset it now needs.
            return reject(mode, http::outcome_code::invalid_argument,
                          "status code is outside 100..599",
                          http1_close_policy::respond_then_close);
        }
        if (s.informational() || s.code() == 204) {
            // RFC 9112 section 6.3 rule 1: no body, and handler framing
            // fields are stripped at emission (the framer's rule).
            return mode;
        }
        if (s.code() == 304) {
            // Metadata only: a Content-Length rides as the would-be GET
            // body size; Transfer-Encoding is stripped at emission.
            mode.kind = http1_response_body_kind::metadata_only;
            return mode;
        }
        if (request.request_method.id() == http::method_id::head) {
            // HEAD: fields verbatim, no framing validation. Migration
            // note: v3 emits the handler's fields exactly and adds no
            // framing, so even an ambiguous CL+TE pair passes through
            // (there is no body to frame).
            mode.kind = http1_response_body_kind::head_no_body;
            return mode;
        }
        const std::span<const std::string> te =
            fields.all("transfer-encoding");
        const std::span<const std::string> cl =
            fields.all("content-length");
        // Both framings on a body response is ambiguous no matter what
        // the values say (the request-side smuggling rule, mirrored).
        if (!te.empty() && !cl.empty()) {
            return reject(mode, http::outcome_code::protocol_error,
                          "Transfer-Encoding together with Content-Length",
                          http1_close_policy::close_now);
        }
        if (!te.empty()) {
            compute_from_te(mode, te);
        } else if (!cl.empty()) {
            compute_from_cl(mode, cl);
        } else if (request.request_protocol == http::protocol::http_1_1) {
            // Engine-selected chunked: the framer appends
            // "Transfer-Encoding: chunked".
            mode.kind = http1_response_body_kind::chunked;
        } else {
            mode.kind = http1_response_body_kind::close_delimited;
        }
        return mode;
    }

 private:
    // Records a typed rejection; the mode's failure is sticky.
    static http1_response_mode& reject(http1_response_mode& mode,
                                       http::outcome_code code,
                                       std::string what,
                                       http1_close_policy policy) {
        mode.failure =
            http::outcome(code, "http1_response_mode: " + std::move(what));
        mode.close_policy = policy;
        return mode;
    }

    // The response side accepts exactly one final "chunked" element;
    // every other spelling is a protocol error. Migration note: the
    // request side answers an unsupported coding with 501 and keeps the
    // connection; a response cannot unsend its head, so the handler-
    // pinned coding list is all-or-nothing and closes.
    static void compute_from_te(http1_response_mode& mode,
                                const std::span<const std::string>& values) {
        if (!is_single_chunked_te(values)) {
            reject(mode, http::outcome_code::protocol_error,
                   "Transfer-Encoding is not exactly one final chunked",
                   http1_close_policy::close_now);
            return;
        }
        mode.kind = http1_response_body_kind::chunked;
    }

    // Exactly one occurrence, whose whole comma-separated list is the
    // single token "chunked" (case-insensitive).
    static bool is_single_chunked_te(
        const std::span<const std::string>& values) {
        if (values.size() != 1) return false;
        std::string_view rest = values.front();
        bool chunked_seen = false;
        for (;;) {
            const std::size_t comma = rest.find(',');
            const std::string_view element =
                detail_head::trim_ows(rest.substr(0, comma));
            const bool is_chunked =
                !element.empty()
                && detail_head::ascii_iequals(element, "chunked");
            if (!is_chunked || chunked_seen) return false;
            chunked_seen = true;
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
        return chunked_seen;
    }

    // The response side mirrors the request-side Content-Length
    // grammar: exactly one occurrence, 1*DIGIT without a leading zero
    // and without overflow.
    static void compute_from_cl(http1_response_mode& mode,
                                const std::span<const std::string>& values) {
        if (values.size() > 1) {
            // Migration note (same as the request side): even identical
            // duplicates are refused — framing-ambiguity resistance.
            reject(mode, http::outcome_code::protocol_error,
                   "duplicate Content-Length",
                   http1_close_policy::close_now);
            return;
        }
        std::uint64_t parsed = 0;
        if (!parse_length(values.front(), parsed)) {
            reject(mode, http::outcome_code::protocol_error,
                   "Content-Length is not a valid digit count",
                   http1_close_policy::close_now);
            return;
        }
        mode.kind = http1_response_body_kind::length;
        mode.content_length = parsed;
    }

    // 1*DIGIT, no leading zero, no overflow (from_chars backend).
    static bool parse_length(std::string_view value, std::uint64_t& out) {
        if (value.empty()) return false;
        for (const char c : value) {
            if (c < '0' || c > '9') return false;
        }
        if (value.size() >= 2 && value.front() == '0') return false;
        return std::from_chars(value.data(), value.data() + value.size(),
                               out, 10)
                   .ec == std::errc();
    }
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_MODE_HPP_
