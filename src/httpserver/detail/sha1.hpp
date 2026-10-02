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

using sha1_mix_fn =
    std::uint32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t);

// Per step-group (20 rounds each): the mix and the additive constant.
inline constexpr std::array<sha1_mix_fn, 4> k_sha1_mix{
    sha1_choice, sha1_parity, sha1_majority, sha1_parity};
inline constexpr std::array<std::uint32_t, 4> k_sha1_group_k{
    0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6};

// One 512-bit block into the five running state words.
inline void sha1_compress(std::uint32_t (&h)[5],
                          const std::byte* block) noexcept {
    std::uint32_t w[80];
    for (int t = 0; t < 16; ++t) {
        w[t] = (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(block[t * 4])) << 24)
             | (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(block[t * 4 + 1])) << 16)
             | (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(block[t * 4 + 2])) << 8)
             | static_cast<std::uint32_t>(
                   static_cast<unsigned char>(block[t * 4 + 3]));
    }
    for (int t = 16; t < 80; ++t) {
        w[t] = sha1_rotate_left(
            w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    }
    std::uint32_t a = h[0];
    std::uint32_t b = h[1];
    std::uint32_t c = h[2];
    std::uint32_t d = h[3];
    std::uint32_t e = h[4];
    for (int t = 0; t < 80; ++t) {
        const int group = t / 20;
        const std::uint32_t temp = sha1_rotate_left(a, 5)
            + k_sha1_mix[group](b, c, d) + e
            + k_sha1_group_k[group] + w[t];
        e = d;
        d = c;
        c = sha1_rotate_left(b, 30);
        b = a;
        a = temp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

inline std::array<std::byte, 20> sha1(std::span<const std::byte> input) {
    std::uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe,
                          0x10325476, 0xc3d2e1f0};
    std::size_t offset = 0;
    while (offset + 64 <= input.size()) {
        sha1_compress(h, input.data() + offset);
        offset += 64;
    }
    // Tail plus padding: 0x80, zeros, and the 64-bit big-endian bit
    // length. 56 bytes of tail push the length field into a second
    // staged block, hence the 128-byte buffer.
    std::byte tail[128] = {};
    const std::size_t rest = input.size() - offset;
    for (std::size_t i = 0; i < rest; ++i) {
        tail[i] = input[offset + i];
    }
    tail[rest] = std::byte{0x80};
    const std::size_t staged = (rest + 9 <= 64) ? 64 : 128;
    const std::uint64_t bits =
        static_cast<std::uint64_t>(input.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[staged - 1 - i] =
            static_cast<std::byte>((bits >> (8 * i)) & 0xff);
    }
    sha1_compress(h, tail);
    if (staged == 128) sha1_compress(h, tail + 64);

    std::array<std::byte, 20> digest{};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[i * 4 + j] =
                static_cast<std::byte>((h[i] >> (24 - 8 * j)) & 0xff);
        }
    }
    return digest;
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_SHA1_HPP_
