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

// TASK-106: authoritative HTTP/1 request-body framing decision
// (PRD-V3N-REQ-004/021, DR-V3-006). NOT part of the installed surface.
//
// One decision, evaluated top to bottom over the parsed head's
// Transfer-Encoding and Content-Length field occurrences (RFC 9112
// section 6.3). The engine computes the mode exactly once, right after
// take(); the decoder and the body adapter consume it and never
// recompute. Every strictness delta vs the v2 (libmicrohttpd-backed)
// framing is pre-approved for v3 and recorded as a migration note at
// its rejection site.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_body_mode.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_BODY_MODE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_BODY_MODE_HPP_

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>

namespace httpserver {

namespace detail {

// The one authoritative framing of a request body. none: no body (no
// framing headers, RFC 9112 section 6.3 rule 4); length: exactly one
// valid Content-Length; chunked: exactly one "chunked" final transfer
// coding; rejected: the head is ambiguous or malformed — the failure
// and close posture say how the engine must proceed.
enum class http1_body_kind : std::uint8_t { none, length, chunked, rejected };

struct http1_body_mode {
    http1_body_kind kind = http1_body_kind::none;
    std::uint64_t content_length = 0;  // length kind only
    http::outcome failure;             // rejected kind only
    http1_close_policy close_policy = http1_close_policy::none;

    // The authoritative decision for one parsed request head. Pure:
    // reads the head, returns the mode, touches nothing else.
    static http1_body_mode compute(const http::request_head& head) {
        http1_body_mode mode;
        const std::span<const std::string> te =
            head.head_fields.all("transfer-encoding");
        const std::span<const std::string> cl =
            head.head_fields.all("content-length");
        // R1: both framings present is a request-smuggling probe, no
        // matter what the values say.
        if (!te.empty() && !cl.empty()) {
            reject(mode, http::outcome_code::protocol_error,
                   "Transfer-Encoding together with Content-Length",
                   http1_close_policy::close_now);
            return mode;
        }
        if (!te.empty()) {
            compute_transfer_encoding(mode, head.request_protocol, te);
        } else {
            compute_content_length(mode, cl);
        }
        return mode;
    }

 private:
    // Records a typed rejection; the mode's kind becomes rejected.
    static void reject(http1_body_mode& mode, http::outcome_code code,
                       std::string what, http1_close_policy policy) {
        mode.kind = http1_body_kind::rejected;
        mode.failure =
            http::outcome(code, "http1_body_mode: " + std::move(what));
        mode.close_policy = policy;
    }

    // R2a-R2f: the Transfer-Encoding branch. The whole list is the
    // concatenation of every field occurrence's comma-separated
    // elements in wire order.
    static void compute_transfer_encoding(
        http1_body_mode& mode, http::protocol version,
        const std::span<const std::string>& values) {
        // R2a: chunked exists only from HTTP/1.1 on.
        if (version == http::protocol::http_1_0) {
            reject(mode, http::outcome_code::protocol_error,
                   "Transfer-Encoding on HTTP/1.0",
                   http1_close_policy::close_now);
            return;
        }
        std::vector<std::string_view> elements;
        if (!split_elements(mode, values, elements)) return;
        classify_chunked(mode, elements);
    }

    // Splits every value on commas into OWS-trimmed elements. R2b:
    // an empty or non-token element rejects (close_now) and returns
    // false.
    static bool split_elements(http1_body_mode& mode,
                               const std::span<const std::string>& values,
                               std::vector<std::string_view>& out) {
        for (const std::string& value : values) {
            std::string_view rest = value;
            for (;;) {
                const std::size_t comma = rest.find(',');
                const std::string_view element =
                    detail_head::trim_ows(rest.substr(0, comma));
                if (element.empty() || !http::detail::is_token(element)) {
                    reject(mode, http::outcome_code::protocol_error,
                           "Transfer-Encoding element is empty or not a"
                           " token",
                           http1_close_policy::close_now);
                    return false;
                }
                out.push_back(element);
                if (comma == std::string_view::npos) break;
                rest.remove_prefix(comma + 1);
            }
        }
        return true;
    }

    // R2c: no chunked element; R2d: chunked exactly once and last;
    // R2e: a coding before the final chunked is a 501 (the message is
    // well-formed, the coding just is not supported); R2f: accept.
    static void classify_chunked(http1_body_mode& mode,
                                 const std::vector<std::string_view>&
                                     elements) {
        bool chunked_seen = false;
        bool coding_before_chunked = false;
        for (const std::string_view element : elements) {
            if (ascii_iequals(element, "chunked")) {
                if (chunked_seen) {
                    reject(mode, http::outcome_code::protocol_error,
                           "chunked appears more than once",
                           http1_close_policy::close_now);
                    return;
                }
                chunked_seen = true;
                continue;
            }
            if (chunked_seen) {
                reject(mode, http::outcome_code::protocol_error,
                       "chunked is not the final transfer coding",
                       http1_close_policy::close_now);
                return;
            }
            coding_before_chunked = true;
        }
        if (!chunked_seen) {
            reject(mode, http::outcome_code::protocol_error,
                   "Transfer-Encoding without chunked",
                   http1_close_policy::close_now);
            return;
        }
        if (coding_before_chunked) {
            reject(mode, http::outcome_code::not_supported,
                   "transfer coding before chunked is not supported",
                   http1_close_policy::respond_then_close);
            return;
        }
        mode.kind = http1_body_kind::chunked;
    }

    // R3a-R3c and R4: the Content-Length branch (no TE present).
    static void compute_content_length(
        http1_body_mode& mode, const std::span<const std::string>& values) {
        // R4: no framing headers at all — no body.
        if (values.empty()) return;
        if (values.size() > 1) {
            // Migration note: RFC 9110 section 8.6 MAYs that
            // recipients combine duplicate Content-Length values; v3
            // declines (even identical duplicates are a classic
            // request-smuggling probe).
            reject(mode, http::outcome_code::protocol_error,
                   "duplicate Content-Length",
                   http1_close_policy::close_now);
            return;
        }
        std::uint64_t parsed = 0;
        if (!parse_length(values.front(), parsed)) {
            // Migration note: a leading-zero spelling such as "007" is
            // RFC-legal (1*DIGIT) but rejected for framing-ambiguity
            // resistance.
            reject(mode, http::outcome_code::protocol_error,
                   "Content-Length is not a valid digit count",
                   http1_close_policy::close_now);
            return;
        }
        mode.kind = http1_body_kind::length;
        mode.content_length = parsed;
    }

    // R3b: 1*DIGIT without a leading zero and without overflow.
    static bool parse_length(std::string_view value, std::uint64_t& out) {
        if (value.empty()) return false;
        for (const char c : value) {
            if (c < '0' || c > '9') return false;
        }
        if (value.size() >= 2 && value.front() == '0') return false;
        constexpr std::uint64_t kMax =
            std::numeric_limits<std::uint64_t>::max();
        std::uint64_t parsed = 0;
        for (const char c : value) {
            const std::uint64_t digit =
                static_cast<std::uint64_t>(c - '0');
            if (parsed > (kMax - digit) / 10) return false;
            parsed = parsed * 10 + digit;
        }
        out = parsed;
        return true;
    }

    // ASCII case-insensitive equality (transfer-coding comparison).
    static bool ascii_iequals(std::string_view a,
                              std::string_view b) noexcept {
        return detail_head::ascii_iequals(a, b);
    }
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_BODY_MODE_HPP_
