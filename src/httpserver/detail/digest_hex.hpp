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
std::string encode_hex(std::span<const std::byte> bytes);

namespace hex_detail {

int hex_digit_value(char c) noexcept;

}  // namespace hex_detail

// Strict hex decode: refuses empty input, odd lengths, and any
// non-hexadecimal octet.
std::optional<std::vector<std::byte>> decode_hex(
    std::string_view text);

}  // namespace digest

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DIGEST_HEX_HPP_
