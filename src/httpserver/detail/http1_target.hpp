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

// Request-target form validation and route_path derivation for the
// strict HTTP/1 head parser (TASK-105, PRD-V3N-REQ-019). Pure
// functions; the caller owns raw_target and route_path storage.
//
// The raw target is kept byte-exact in request_head::raw_target; the
// route path is DERIVED. The pipeline mirrors the v2 one (unescape ->
// standardize -> normalize): decode the path, then resolve dot
// segments and collapse slashes, so route_path is always the absolute,
// decoded, dot-resolved, slash-canonical form route matching expects.
//
// Strictness deltas vs v2 (pre-approved for v3, pinned by
// test/unit/http1_parser_test.cpp, recorded at the rejection sites):
//   2. invalid percent-escapes: v2 passed them through, v3 rejects.
//   3. NUL in the target: v2 truncated at NUL, v3 rejects.
//   4. empty request-target: v2 canonicalized "" to "/", v3 rejects
//      origin-form targets not starting with '/'.
//   5. '+' decoded as space in the path: PRESERVED from v2 (pinned at
//      the decode site).
//
// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units; the #error check intentionally precedes the guard.
#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_target.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_TARGET_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_TARGET_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/detail/unescape_helpers.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

namespace http1_target {

// Request-target forms (RFC 9112 section 3.2).
enum class form : std::uint8_t {
    origin,     // absolute path plus optional query: "/path[?query]"
    absolute,   // "http://authority[...]" with an http/https scheme
    authority,  // "host[:port]" — CONNECT only
    asterisk,   // "*" — OPTIONS only
};

// One target's selected form plus the origin-form portion to process.
// origin is empty for the authority and asterisk forms, which carry no
// path.
struct target_selection {
    form shape = form::origin;
    std::string_view origin;
};

// True iff raw carries a byte no request-target may contain: SP (the
// request-line separator), any CTL byte, or '#' (a fragment delimiter
// may not appear in a request-target).
bool contains_forbidden_byte(std::string_view raw) noexcept;

char lower_ascii(char c) noexcept;

bool equals_folded(std::string_view a, std::string_view b) noexcept;

// RFC 9112 section 3.2.2: this server speaks http/https, compared
// case-insensitively.
inline bool is_http_scheme(std::string_view scheme) noexcept {
    return equals_folded(scheme, "http")
        || equals_folded(scheme, "https");
}

// CONNECT admits only the authority form: a non-empty host[:port] with
// no path or query delimiters. The route path is the authority
// verbatim (handled by the orchestrator).
http::outcome select_connect_target(std::string_view raw,
                                           target_selection& out);

// Non-CONNECT targets: origin form, or absolute form with an
// http/https scheme (RFC 9112 section 3.2.2: accept it and process the
// origin remainder). The authority runs to the first '/', '?' or the
// end; an empty remainder yields the root at decode time.
http::outcome select_path_target(std::string_view raw,
                                        target_selection& out);

// Selects and validates the request-target form for (method, raw).
// protocol_error when raw is empty, carries a forbidden byte, matches
// no form, mismatches the method (asterisk is OPTIONS-only, authority
// is CONNECT-only, CONNECT accepts no other form), or carries an
// absolute-form scheme other than http/https.
http::outcome select_target_form(const http::method& m,
                                        std::string_view raw,
                                        target_selection& out);

// Strict percent-decoding of the path portion. Every %XX decodes,
// including %2F ('/') and %20 (space): decoded bytes are path data.
// '+' maps to space — preserved from the v2 pipeline, pinned by test
// (the query string is not decoded here; it is dropped for routing and
// kept byte-exact in raw_target). Migration notes (deltas 2-3): v2
// passed invalid escapes through and truncated at NUL; v3 rejects
// both.
http::outcome decode_path(std::string_view raw, std::string& out);

// True iff a path segment survives normalization: not the empty
// segment (a repeated slash collapses), not ".".
inline bool keeps_segment(std::string_view segment) noexcept {
    return !segment.empty() && segment != ".";
}

// Dot-segment resolution and slash canonicalization with the v2
// normalize_path semantics: empty segments collapse, "." segments drop,
// ".." pops the last retained segment (silently dropped at the root),
// the result always carries a leading slash and never a trailing slash
// except for the root itself. Never longer than the input.
http::outcome resolve_dot_segments(std::string_view path,
                                          std::string& route);

// Derives route_path from (method, raw_target): the form-consistent,
// decoded, dot-resolved, slash-canonical input to route matching.
// route_path never exceeds raw_target plus one byte (only an empty
// absolute-form path grows, to "/").
http::outcome derive_route_path(const http::method& m,
                                       std::string_view raw_target,
                                       std::string& route_path);

}  // namespace http1_target

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_TARGET_HPP_
