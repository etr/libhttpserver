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

// The RFC 7616 section 3.4 response computation (TASK-115, plan
// section 5). ONE algorithm-parameterized path -- a hash_descriptor
// carries the wire token, the digest width, and H() as a lowercase-hex
// function -- so MD5 and SHA-256 share every line of chain assembly
// and a third algorithm would be one more descriptor, not a parallel
// block (the duplication gate's requirement). The non-session
// formulas only (v2 parity: MD5-sess/SHA-256-sess were never
// exposed):
//
//   HA1      = H(username ":" realm ":" password)
//   HA2      = H(method ":" uri)                  [qop=auth]
//   response = H(HA1 ":" nonce ":" nc ":" cnonce ":" qop ":" HA2)
//   legacy   = H(HA1 ":" nonce ":" HA2)           [no qop, RFC 2617]
//
// Every input arrives as a view over already-decoded parser output
// (digest_params.hpp) or policy state; nothing here parses, locks, or
// allocates beyond the joined chain and its hex digest.

#if !defined(HTTPSERVER_COMPILATION)
#error "digest_response.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DIGEST_RESPONSE_HPP_
#define SRC_HTTPSERVER_DETAIL_DIGEST_RESPONSE_HPP_

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

#include <httpserver/detail/digest_hex.hpp>
#include <httpserver/detail/md5.hpp>
#include <httpserver/detail/sha256.hpp>

namespace httpserver {

namespace detail {

namespace digest {

namespace response_detail {

inline std::span<const std::byte> raw_bytes(std::string_view text) noexcept {
    const auto* raw = reinterpret_cast<const std::byte*>(text.data());
    return std::span<const std::byte>(raw, text.size());
}

// "a:b:c" over any number of parts.
inline std::string colon_joined(
    std::initializer_list<std::string_view> parts) {
    std::string out;
    for (const std::string_view part : parts) {
        if (!out.empty()) out.push_back(':');
        out.append(part);
    }
    return out;
}

}  // namespace response_detail

// The one algorithm axis of the Digest computation. token is the
// RFC 7616 wire spelling the policy matches client values against;
// digest_size is the byte width (2x the hex length of HA1/HA2 and
// the response); hex_of is H() as canonical lowercase hex.
struct hash_descriptor {
    std::string_view token;
    std::size_t digest_size;
    std::string (*hex_of)(std::string_view);
};

inline std::string md5_hex(std::string_view text) {
    return encode_hex(md5(response_detail::raw_bytes(text)));
}

inline std::string sha256_hex(std::string_view text) {
    return encode_hex(sha256(response_detail::raw_bytes(text)));
}

inline constexpr hash_descriptor md5_descriptor{"MD5", 16, &md5_hex};
inline constexpr hash_descriptor sha256_descriptor{"SHA-256", 32,
                                                   &sha256_hex};

// HA1 = H(username:realm:password); the fixed-credential factory
// computes this once and scrubs the cleartext, the HA1-source form
// receives it from the application.
inline std::string compute_ha1(const hash_descriptor& descriptor,
                               std::string_view username,
                               std::string_view realm,
                               std::string_view password) {
    return descriptor.hex_of(response_detail::colon_joined(
        {username, realm, password}));
}

// HA2 = H(method:uri) for qop=auth (auth-int is rejected at the
// policy, never computed here).
inline std::string compute_ha2(const hash_descriptor& descriptor,
                               std::string_view method,
                               std::string_view uri) {
    return descriptor.hex_of(
        response_detail::colon_joined({method, uri}));
}

// The qop-present chain (RFC 7616 section 3.4.1).
inline std::string compute_response_qop(
    const hash_descriptor& descriptor, std::string_view ha1_hex,
    std::string_view nonce, std::string_view nc, std::string_view cnonce,
    std::string_view qop, std::string_view ha2_hex) {
    return descriptor.hex_of(response_detail::colon_joined(
        {ha1_hex, nonce, nc, cnonce, qop, ha2_hex}));
}

// The legacy chain for a request that carried no qop (RFC 2617
// section 3.2.2.1 compatibility): response = H(HA1:nonce:HA2).
inline std::string compute_response_legacy(
    const hash_descriptor& descriptor, std::string_view ha1_hex,
    std::string_view nonce, std::string_view ha2_hex) {
    return descriptor.hex_of(
        response_detail::colon_joined({ha1_hex, nonce, ha2_hex}));
}

}  // namespace digest

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DIGEST_RESPONSE_HPP_
