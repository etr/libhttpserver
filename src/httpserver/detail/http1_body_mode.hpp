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
// recompute. Every strictness delta vs the v2
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
    static http1_body_mode compute(const http::request_head& head);

 private:
    // Records a typed rejection; the mode's kind becomes rejected.
    static void reject(http1_body_mode& mode, http::outcome_code code,
                       std::string what, http1_close_policy policy);

    // R2a-R2f: the Transfer-Encoding branch. The whole list is the
    // concatenation of every field occurrence's comma-separated
    // elements in wire order.
    static void compute_transfer_encoding(
        http1_body_mode& mode, http::protocol version,
        const std::span<const std::string>& values);

    // Splits every value on commas into OWS-trimmed elements. R2b:
    // an empty or non-token element rejects (close_now) and returns
    // false.
    static bool split_elements(http1_body_mode& mode,
                               const std::span<const std::string>& values,
                               std::vector<std::string_view>& out);

    // R2c: no chunked element; R2d: chunked exactly once and last;
    // R2e: a coding before the final chunked is a 501 (the message is
    // well-formed, the coding just is not supported); R2f: accept.
    static void classify_chunked(http1_body_mode& mode,
                                 const std::vector<std::string_view>&
                                     elements);

    // R3a-R3c and R4: the Content-Length branch (no TE present).
    static void compute_content_length(
        http1_body_mode& mode, const std::span<const std::string>& values);

    // R3b: 1*DIGIT without a leading zero and without overflow.
    static bool parse_length(std::string_view value, std::uint64_t& out);

    // ASCII case-insensitive equality (transfer-coding comparison).
    static bool ascii_iequals(std::string_view a,
                              std::string_view b) noexcept {
        return detail_head::ascii_iequals(a, b);
    }
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_BODY_MODE_HPP_
