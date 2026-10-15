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
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// In-tree Base64 codec (RFC 4648) for the native v3 surface
// (TASK-114, architecture section 4 / DR-V3-001). Two consumers are
// planned: the Basic credentials token of RFC 7617 and the RFC 6455
// upgrade-handshake accept key. Both decode hostile input, so the
// decoder is strictly canonical: the input length must be a multiple
// of four, '=' may appear only as the one or two trailing characters
// of the final quantum, every other character must be from the
// standard alphabet ('+' and '/'; the url-safe variant is a different
// codec), and the trailing bits carried past the final octet must be
// zero. Anything else is rejected rather than repaired.

#if !defined(HTTPSERVER_COMPILATION)
#error "base64.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_BASE64_HPP_
#define SRC_HTTPSERVER_DETAIL_BASE64_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace httpserver {

namespace detail {

constexpr std::string_view k_base64_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::uint32_t octet(const std::byte b) noexcept {
    return static_cast<std::uint32_t>(b);
}

// Appends the four characters of one full 24-bit quantum.
void encode_quantum(std::string& out, std::uint32_t bits);

std::string base64_encode(std::span<const std::byte> input);

// Alphabet position of a character, or -1 outside the alphabet.
int base64_value(const char c) noexcept;

// Number of trailing '=' padding characters (0, 1, or 2). Anything
// else padding-shaped is rejected later by the alphabet scan.
std::size_t base64_padding(std::string_view input) noexcept;

// Appends the three octets of one full 24-bit quantum.
void decode_quantum(std::vector<std::byte>& out,
                           std::uint32_t bits);

// Emits the final short quantum (2 or 3 alphabet characters). The
// trailing bits carried past the final octet must be zero: a nonzero
// tail means the input was not produced by a canonical encoder and is
// rejected rather than repaired.
bool decode_final_quantum(std::vector<std::byte>& out,
                                 std::uint32_t bits, int count);

std::optional<std::vector<std::byte>> base64_decode(
    std::string_view input);

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_BASE64_HPP_
