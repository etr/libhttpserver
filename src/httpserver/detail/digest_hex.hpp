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

// The hex codec shared by every RFC 7616 Digest surface (TASK-115,
// architecture section: nonce/opaque material, HA1, HA2 and the
// response token are all lowercase hex). One codec, not one per
// algorithm: MD5 and SHA-256 differ only in digest size, which the
// span carries.
//
//   - encode_hex: canonical lowercase output (the form the server
//     emits and the RFC's examples use);
//   - decode_hex: strict -- even-length, non-empty, [0-9a-fA-F] only
//     (clients may send uppercase); anything else is std::nullopt so
//     every caller maps it to one malformed classification instead of
//     half-parsed state.

#if !defined(HTTPSERVER_COMPILATION)
#error "digest_hex.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DIGEST_HEX_HPP_
#define SRC_HTTPSERVER_DETAIL_DIGEST_HEX_HPP_

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace httpserver {

namespace detail {

namespace digest {

// Lowercase hex, two digits per byte.
inline std::string encode_hex(std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        const unsigned value = static_cast<unsigned>(b);
        out.push_back(digits[(value >> 4) & 0xf]);
        out.push_back(digits[value & 0xf]);
    }
    return out;
}

namespace hex_detail {

inline int hex_digit_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace hex_detail

// Strict hex decode: refuses empty input, odd lengths, and any
// non-hexadecimal octet.
inline std::optional<std::vector<std::byte>> decode_hex(
    std::string_view text) {
    using hex_detail::hex_digit_value;
    if (text.empty() || text.size() % 2 != 0) return std::nullopt;
    std::vector<std::byte> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = hex_digit_value(text[i]);
        const int low = hex_digit_value(text[i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<std::byte>((high << 4) | low));
    }
    return out;
}

}  // namespace digest

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DIGEST_HEX_HPP_
