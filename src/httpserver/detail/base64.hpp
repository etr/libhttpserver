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
inline void encode_quantum(std::string& out, std::uint32_t bits) {
    out.push_back(k_base64_alphabet[(bits >> 18) & 0x3f]);
    out.push_back(k_base64_alphabet[(bits >> 12) & 0x3f]);
    out.push_back(k_base64_alphabet[(bits >> 6) & 0x3f]);
    out.push_back(k_base64_alphabet[bits & 0x3f]);
}

inline std::string base64_encode(std::span<const std::byte> input) {
    std::string out;
    out.reserve((input.size() / 3 + 1) * 4);
    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        encode_quantum(out, (octet(input[i]) << 16)
                                | (octet(input[i + 1]) << 8)
                                | octet(input[i + 2]));
        i += 3;
    }
    const std::size_t rest = input.size() - i;
    if (rest == 1) {
        const std::uint32_t bits = octet(input[i]) << 16;
        out.push_back(k_base64_alphabet[(bits >> 18) & 0x3f]);
        out.push_back(k_base64_alphabet[(bits >> 12) & 0x3f]);
        out.append("==");
    } else if (rest == 2) {
        const std::uint32_t bits =
            (octet(input[i]) << 16) | (octet(input[i + 1]) << 8);
        out.push_back(k_base64_alphabet[(bits >> 18) & 0x3f]);
        out.push_back(k_base64_alphabet[(bits >> 12) & 0x3f]);
        out.push_back(k_base64_alphabet[(bits >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

// Alphabet position of a character, or -1 outside the alphabet.
inline int base64_value(const char c) noexcept {
    const std::size_t pos = k_base64_alphabet.find(c);
    return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
}

// Number of trailing '=' padding characters (0, 1, or 2). Anything
// else padding-shaped is rejected later by the alphabet scan.
inline std::size_t base64_padding(std::string_view input) noexcept {
    if (input.empty() || input.back() != '=') return 0;
    if (input.size() >= 2 && input[input.size() - 2] == '=') return 2;
    return 1;
}

// Appends the three octets of one full 24-bit quantum.
inline void decode_quantum(std::vector<std::byte>& out,
                           std::uint32_t bits) {
    out.push_back(static_cast<std::byte>((bits >> 16) & 0xff));
    out.push_back(static_cast<std::byte>((bits >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(bits & 0xff));
}

// Emits the final short quantum (2 or 3 alphabet characters). The
// trailing bits carried past the final octet must be zero: a nonzero
// tail means the input was not produced by a canonical encoder and is
// rejected rather than repaired.
inline bool decode_final_quantum(std::vector<std::byte>& out,
                                 std::uint32_t bits, int count) {
    if (count == 2) {
        if ((bits & 0xf) != 0) return false;
        out.push_back(static_cast<std::byte>(bits >> 4));
    } else if (count == 3) {
        if ((bits & 0x3) != 0) return false;
        out.push_back(static_cast<std::byte>(bits >> 10));
        out.push_back(static_cast<std::byte>((bits >> 2) & 0xff));
    }
    return true;
}

inline std::optional<std::vector<std::byte>> base64_decode(
    std::string_view input) {
    if (input.size() % 4 != 0) return std::nullopt;
    const std::size_t pad = base64_padding(input);
    std::vector<std::byte> out;
    out.reserve(input.size() / 4 * 3);
    std::uint32_t bits = 0;
    int count = 0;
    for (std::size_t i = 0; i < input.size() - pad; ++i) {
        const int value = base64_value(input[i]);
        if (value < 0) return std::nullopt;
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        if (++count == 4) {
            decode_quantum(out, bits);
            bits = 0;
            count = 0;
        }
    }
    if (!decode_final_quantum(out, bits, count)) return std::nullopt;
    return out;
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_BASE64_HPP_
