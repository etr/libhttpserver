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
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

// The bounded Authorization: Digest field parser (TASK-115, plan
// section 5; RFC 7616 section 3.4 over the RFC 7235 credentials
// grammar). One classification for every failure -- params_status::
// malformed -- because the v2 posture answers bad credentials with
// the same 401 challenge as absent ones, never a 400.
//
// Grammar accepted (case-insensitive scheme and param names, OWS
// around every separator, quoted-string values with quoted-pair
// unescaping, token values otherwise):
//
//   credentials = "Digest" 1*( SP / HTAB ) #( auth-param )
//   auth-param  = name BWS "=" BWS ( quoted-string / token )
//
// Documented bounds (every overrun is malformed, so a hostile field
// cannot make the parse allocate without limit):
//   - the whole value is at most 8192 bytes;
//   - at most 24 auth-params;
//   - a name is at most 24 bytes, a (decoded) value at most 4096;
//   - nc, when present, is exactly 8 hexadecimal digits;
//   - no control octet (CTL, including NUL/CR/LF) anywhere: OWS is
//     SP/HTAB only and escaped CTLs are refused;
//   - a duplicated KNOWN param name is malformed; unknown params are
//     ignored (RFC 7616 section 3.3), duplicates included;
//   - the five mandatory params (username, realm, nonce, uri,
//     response) must be present and non-empty.
//
// Semantic checks that depend on the policy's configured algorithm
// (qop=auth vs auth-int, the algorithm token, the response's hex
// width) live in the policy, not here: the parser only establishes
// that the field is a well-formed Digest credential.

#if !defined(HTTPSERVER_COMPILATION)
#error "digest_params.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DIGEST_PARAMS_HPP_
#define SRC_HTTPSERVER_DETAIL_DIGEST_PARAMS_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/detail/auth_text.hpp>

namespace httpserver {

namespace detail {

namespace digest {

using detail::auth_text::ascii_iequal;

struct digest_params {
    std::string username;
    std::string realm;
    std::string nonce;
    std::string uri;
    std::string response;
    std::string algorithm;  // verbatim token, empty when absent
    std::string qop;        // verbatim token, empty when absent
    std::string cnonce;
    std::string nc;         // exactly 8 hex digits, empty when absent
    std::string opaque;
};

enum class params_status : std::uint8_t {
    ok,
    malformed,
};

namespace params_detail {

inline constexpr std::size_t k_max_whole = 8192;
inline constexpr std::size_t k_max_params = 24;
inline constexpr std::size_t k_max_name = 24;
inline constexpr std::size_t k_max_value = 4096;

// RFC 7230 section 3.2.6 tchar; '=' and CTLs are excluded by design.
inline constexpr std::string_view k_tchar_specials = "!#$%&'*+-.^_`|~";

inline bool is_tchar(char c) noexcept {
    if (c >= '0' && c <= '9') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    return k_tchar_specials.find(c) != std::string_view::npos;
}

inline bool is_ctl(char c) noexcept {
    const unsigned value = static_cast<unsigned char>(c);
    return value < 0x20 || value == 0x7f;
}

inline bool is_ows(char c) noexcept {
    return c == ' ' || c == '\t';
}

inline void skip_ows(std::string_view v, std::size_t& pos) noexcept {
    while (pos < v.size() && is_ows(v[pos])) ++pos;
}

inline bool is_eight_hex(const std::string& value) noexcept {
    if (value.size() != 8) return false;
    for (const char c : value) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        const bool upper = c >= 'A' && c <= 'F';
        if (!digit && !lower && !upper) return false;
    }
    return true;
}

// Consumes the case-insensitive "Digest" scheme plus its mandatory
// 1*(SP/HTAB) separator; at least one param must follow.
inline bool match_scheme(std::string_view v, std::size_t& pos) noexcept {
    std::size_t i = 0;
    while (i < v.size() && is_tchar(v[i])) ++i;
    if (!ascii_iequal(v.substr(0, i), "Digest")) return false;
    std::size_t separator = i;
    while (separator < v.size() && is_ows(v[separator])) ++separator;
    if (separator == i || separator == v.size()) return false;
    pos = separator;
    return true;
}

// Reads `name BWS "="`, consuming the '='; the name is 1..24 tchars.
inline bool parse_name(std::string_view v, std::size_t& pos,
                       std::string& name) {
    const std::size_t start = pos;
    while (pos < v.size() && is_tchar(v[pos])) ++pos;
    const std::size_t end = pos;
    if (end == start || end - start > k_max_name) return false;
    skip_ows(v, pos);
    if (pos == v.size() || v[pos] != '=') return false;
    name.assign(v.substr(start, end - start));
    ++pos;
    return true;
}

// Reads a quoted-string (opening quote at v[pos]): quoted-pair
// unescaping, no control octet (escaped or raw), decoded value capped
// at k_max_value. pos lands after the closing quote.
inline bool parse_quoted(std::string_view v, std::size_t& pos,
                         std::string& out) {
    ++pos;
    for (;;) {
        if (pos == v.size()) return false;  // unterminated quote
        const char c = v[pos];
        if (is_ctl(c)) return false;
        if (c == '"') {
            ++pos;
            return true;
        }
        if (c == '\\') {
            if (pos + 1 >= v.size()) return false;
            if (is_ctl(v[pos + 1])) return false;
            out.push_back(v[pos + 1]);
            pos += 2;
        } else {
            out.push_back(c);
            ++pos;
        }
        if (out.size() > k_max_value) return false;
    }
}

// Reads one value after `=` BWS: a quoted-string or a non-empty
// token.
inline bool parse_value(std::string_view v, std::size_t& pos,
                        std::string& out) {
    skip_ows(v, pos);
    if (pos == v.size()) return false;
    if (v[pos] == '"') return parse_quoted(v, pos, out);
    const std::size_t start = pos;
    while (pos < v.size() && is_tchar(v[pos])) ++pos;
    if (pos == start || pos - start > k_max_value) return false;
    out.assign(v.substr(start, pos - start));
    return true;
}

// One auth-param plus its list separator: OWS name BWS "=" BWS value
// OWS [ "," OWS ] -- and a trailing comma is an empty list element,
// i.e. malformed.
inline bool parse_one(std::string_view v, std::size_t& pos,
                      std::string& name, std::string& value) {
    if (v[pos] == ',') return false;
    if (!parse_name(v, pos, name)) return false;
    if (!parse_value(v, pos, value)) return false;
    skip_ows(v, pos);
    if (pos < v.size()) {
        if (v[pos] != ',') return false;
        ++pos;
        skip_ows(v, pos);
        if (pos == v.size()) return false;
    }
    return true;
}

struct param_row {
    std::string_view name;
    std::string digest_params::*field;
};

inline constexpr param_row k_param_rows[] = {
    {"username", &digest_params::username},
    {"realm", &digest_params::realm},
    {"nonce", &digest_params::nonce},
    {"uri", &digest_params::uri},
    {"response", &digest_params::response},
    {"algorithm", &digest_params::algorithm},
    {"qop", &digest_params::qop},
    {"cnonce", &digest_params::cnonce},
    {"nc", &digest_params::nc},
    {"opaque", &digest_params::opaque},
};

// Files one parsed param: a duplicated known name refuses, nc must be
// eight hex digits, unknown names are ignored. Returns false on
// malformed.
inline bool assign_param(digest_params& parsed, unsigned& seen,
                         const std::string& name, std::string&& value) {
    for (std::size_t i = 0; i < std::size(k_param_rows); ++i) {
        if (!ascii_iequal(name, k_param_rows[i].name)) continue;
        const unsigned bit = 1u << i;
        if ((seen & bit) != 0) return false;
        if (k_param_rows[i].name == "nc" && !is_eight_hex(value)) {
            return false;
        }
        seen |= bit;
        parsed.*(k_param_rows[i].field) = std::move(value);
        return true;
    }
    return true;
}

// The five mandatory params, present and non-empty.
inline bool mandatories_present(const digest_params& parsed) noexcept {
    return !parsed.username.empty() && !parsed.realm.empty()
           && !parsed.nonce.empty() && !parsed.uri.empty()
           && !parsed.response.empty();
}

}  // namespace params_detail

// Parses the Authorization field value (scheme included). On ok, @p
// out holds the decoded params; on malformed, @p out is untouched.
inline params_status parse_digest_credentials(
    std::string_view value, digest_params& out) {
    using params_detail::assign_param;
    using params_detail::k_max_params;
    using params_detail::k_max_whole;
    using params_detail::mandatories_present;
    using params_detail::match_scheme;
    using params_detail::parse_one;
    if (value.size() > k_max_whole) return params_status::malformed;
    std::size_t pos = 0;
    if (!match_scheme(value, pos)) return params_status::malformed;

    digest_params parsed;
    unsigned seen = 0;
    std::size_t count = 0;
    for (;;) {
        params_detail::skip_ows(value, pos);
        if (pos == value.size()) break;
        if (++count > k_max_params) return params_status::malformed;
        std::string name;
        std::string param_value;
        if (!parse_one(value, pos, name, param_value)) {
            return params_status::malformed;
        }
        if (!assign_param(parsed, seen, name, std::move(param_value))) {
            return params_status::malformed;
        }
    }
    if (!mandatories_present(parsed)) return params_status::malformed;
    out = std::move(parsed);
    return params_status::ok;
}

}  // namespace digest

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DIGEST_PARAMS_HPP_
