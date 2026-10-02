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

// In-tree MD5 (RFC 1321) for the native v3 surface (TASK-114,
// architecture section 4 / DR-V3-001). MD5 remains a REQUIRED part of
// RFC 7616 Digest authentication and this library's documented v2
// behavior exposes it, so the TLS-off build keeps a faithful in-tree
// implementation; it is never used outside the Digest computation the
// protocol names it for. The implementation follows RFC 1321's own
// presentation: little-endian words, the four 16-round step groups
// with their F/G/H/I mixes, the per-group message-word index
// schedules, and the sine-derived additive table T.

#if !defined(HTTPSERVER_COMPILATION)
#error "md5.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_MD5_HPP_
#define SRC_HTTPSERVER_DETAIL_MD5_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace httpserver {

namespace detail {

// RFC 1321 section 3.4: T[i] = floor(2^32 * abs(sin(i))).
inline constexpr std::array<std::uint32_t, 64> k_md5_t{
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf,
    0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
    0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e,
    0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6,
    0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
    0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039,
    0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
    0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
    0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

// Per-round left-rotation amounts (RFC 1321 section 3.4).
inline constexpr std::array<int, 64> k_md5_shift{
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

inline constexpr std::uint32_t md5_rotate_left(std::uint32_t value,
                                               int bits) noexcept {
    return (value << bits) | (value >> (32 - bits));
}

// Message-word index of round i under the four group schedules:
// i, (5i+1), (3i+5), 7i — each mod 16.
inline constexpr int md5_word_index(const int round) noexcept {
    if (round < 16) return round;
    if (round < 32) return (5 * round + 1) % 16;
    if (round < 48) return (3 * round + 5) % 16;
    return (7 * round) % 16;
}

// The RFC's step: a = b + rotl(a + mix + X[k] + T[i], s).
inline constexpr std::uint32_t md5_step(std::uint32_t a, std::uint32_t mix,
                                        std::uint32_t b, std::uint32_t x_k,
                                        std::uint32_t t_i,
                                        int s) noexcept {
    return b + md5_rotate_left(a + mix + x_k + t_i, s);
}

// One 512-bit block into the four running state words (little-endian).
inline void md5_compress(std::uint32_t (&abcd)[4],
                         const std::byte* block) noexcept {
    std::uint32_t x[16];
    for (int j = 0; j < 16; ++j) {
        x[j] = static_cast<std::uint32_t>(
                   static_cast<unsigned char>(block[j * 4]))
             | (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(block[j * 4 + 1])) << 8)
             | (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(block[j * 4 + 2])) << 16)
             | (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(block[j * 4 + 3])) << 24);
    }
    std::uint32_t a = abcd[0];
    std::uint32_t b = abcd[1];
    std::uint32_t c = abcd[2];
    std::uint32_t d = abcd[3];
    for (int i = 0; i < 64; ++i) {
        const int k = md5_word_index(i);
        const int s = k_md5_shift[i];
        const std::uint32_t t = k_md5_t[i];
        std::uint32_t mixed = c ^ (b | ~d);
        if (i < 48) mixed = b ^ c ^ d;
        if (i < 32) mixed = (b & d) | (c & ~d);
        if (i < 16) mixed = (b & c) | (~b & d);
        const std::uint32_t stepped =
            md5_step(a, mixed, b, x[k], t, s);
        a = d;
        d = c;
        c = b;
        b = stepped;
    }
    abcd[0] += a;
    abcd[1] += b;
    abcd[2] += c;
    abcd[3] += d;
}

inline std::array<std::byte, 16> md5(std::span<const std::byte> message) {
    std::uint32_t abcd[4] = {0x67452301, 0xefcdab89,
                             0x98badcfe, 0x10325476};
    std::size_t done = 0;
    while (done + 64 <= message.size()) {
        md5_compress(abcd, message.data() + done);
        done += 64;
    }
    // MD5 pads with 0x80 then zeros to a 56-byte boundary, then the
    // 64-bit LITTLE-endian bit length; 55 or 56 tail bytes need a
    // second staged block.
    std::byte tail[128] = {};
    const std::size_t remaining = message.size() - done;
    for (std::size_t i = 0; i < remaining; ++i) {
        tail[i] = message[done + i];
    }
    tail[remaining] = std::byte{0x80};
    const std::size_t total = (remaining < 56) ? 64 : 128;
    const std::uint64_t bit_count =
        static_cast<std::uint64_t>(message.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[total - 8 + i] =
            static_cast<std::byte>((bit_count >> (8 * i)) & 0xff);
    }
    md5_compress(abcd, tail);
    if (total == 128) md5_compress(abcd, tail + 64);

    std::array<std::byte, 16> digest{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[i * 4 + j] =
                static_cast<std::byte>((abcd[i] >> (8 * j)) & 0xff);
        }
    }
    return digest;
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_MD5_HPP_
