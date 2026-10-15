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

// In-tree SHA-1 (FIPS 180-4 / RFC 6234) for the native v3 surface
// (TASK-114, architecture section 4 / DR-V3-001). SHA-1 is NOT a
// security primitive here: its only planned use is the RFC 6455
// upgrade-handshake accept computation, which the protocol fixes at
// SHA-1 by definition. The implementation is the textbook one-shot
// Merkle-Damgard construction: big-endian words, a 16-word message
// schedule expanded to 80, and four 20-round step groups with the
// spec's Ch / Parity / Maj / Parity selection.

#if !defined(HTTPSERVER_COMPILATION)
#error "sha1.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_SHA1_HPP_
#define SRC_HTTPSERVER_DETAIL_SHA1_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace httpserver {

namespace detail {

inline constexpr std::uint32_t sha1_rotate_left(std::uint32_t value,
                                                int bits) noexcept {
    return (value << bits) | (value >> (32 - bits));
}

// The three boolean mixes of the spec's four 20-round step groups.
inline constexpr std::uint32_t sha1_choice(std::uint32_t b, std::uint32_t c,
                                           std::uint32_t d) noexcept {
    return (b & c) | (~b & d);
}

inline constexpr std::uint32_t sha1_parity(std::uint32_t b, std::uint32_t c,
                                           std::uint32_t d) noexcept {
    return b ^ c ^ d;
}

inline constexpr std::uint32_t sha1_majority(std::uint32_t b, std::uint32_t c,
                                             std::uint32_t d) noexcept {
    return (b & c) | (b & d) | (c & d);
}

using sha1_mix_fn = std::uint32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t);

// Per step-group (20 rounds each): the mix and the additive constant.
inline constexpr std::array<sha1_mix_fn, 4> k_sha1_mix{
    sha1_choice, sha1_parity, sha1_majority, sha1_parity,
};
inline constexpr std::array<std::uint32_t, 4> k_sha1_group_k{
    0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6,
};

// One 512-bit block into the five running state words.
void sha1_compress(std::uint32_t (&h)[5],
                          const std::byte* block) noexcept;

std::array<std::byte, 20> sha1(std::span<const std::byte> input);

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_SHA1_HPP_
