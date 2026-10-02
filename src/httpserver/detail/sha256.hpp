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

// In-tree SHA-256 (FIPS 180-4 / RFC 6234) for the native v3 surface
// (TASK-114, architecture section 4 / DR-V3-001): the SHA-256 variant
// of the documented RFC 7616 Digest algorithms. FIPS 180-4's own
// structure: big-endian words, the 64-word message schedule built
// from the two small sigmas, and the eight-word working register
// chained through the Sigma/Ch/Maj functions.

#if !defined(HTTPSERVER_COMPILATION)
#error "sha256.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_SHA256_HPP_
#define SRC_HTTPSERVER_DETAIL_SHA256_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace httpserver {

namespace detail {

// Round constants: fractional parts of the cube roots of the first
// 64 primes (FIPS 180-4 section 4.2.2).
inline constexpr std::array<std::uint32_t, 64> k_sha256_k{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline constexpr std::uint32_t sha256_rotate_right(
    std::uint32_t value, int bits) noexcept {
    return (value >> bits) | (value << (32 - bits));
}

// The two schedule-expansion sigmas (FIPS 180-4 section 4.1.2).
inline constexpr std::uint32_t sha256_sigma0(std::uint32_t x) noexcept {
    return sha256_rotate_right(x, 7) ^ sha256_rotate_right(x, 18)
           ^ (x >> 3);
}

inline constexpr std::uint32_t sha256_sigma1(std::uint32_t x) noexcept {
    return sha256_rotate_right(x, 17) ^ sha256_rotate_right(x, 19)
           ^ (x >> 10);
}

// The two big-register Sigmas plus Ch and Maj.
inline constexpr std::uint32_t sha256_big_sigma0(
    std::uint32_t x) noexcept {
    return sha256_rotate_right(x, 2) ^ sha256_rotate_right(x, 13)
           ^ sha256_rotate_right(x, 22);
}

inline constexpr std::uint32_t sha256_big_sigma1(
    std::uint32_t x) noexcept {
    return sha256_rotate_right(x, 6) ^ sha256_rotate_right(x, 11)
           ^ sha256_rotate_right(x, 25);
}

inline constexpr std::uint32_t sha256_choose(std::uint32_t e,
                                             std::uint32_t f,
                                             std::uint32_t g) noexcept {
    return (e & f) ^ (~e & g);
}

inline constexpr std::uint32_t sha256_majority(std::uint32_t a,
                                               std::uint32_t b,
                                               std::uint32_t c) noexcept {
    return (a & b) ^ (a & c) ^ (b & c);
}

inline std::uint32_t sha256_load_word(const std::byte* p) noexcept {
    std::uint32_t word = 0;
    for (int i = 0; i < 4; ++i) {
        word = (word << 8)
             | static_cast<std::uint32_t>(
                   static_cast<unsigned char>(p[i]));
    }
    return word;
}

inline void sha256_store_word(std::byte* p,
                              std::uint32_t word) noexcept {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::byte>((word >> (24 - 8 * i)) & 0xff);
    }
}

// One 512-bit block into the eight running state words.
inline void sha256_compress(std::uint32_t (&h)[8],
                            const std::byte* block) noexcept {
    std::uint32_t w[64];
    for (int t = 0; t < 16; ++t) {
        w[t] = sha256_load_word(block + t * 4);
    }
    for (int t = 16; t < 64; ++t) {
        w[t] = sha256_sigma1(w[t - 2]) + w[t - 7]
             + sha256_sigma0(w[t - 15]) + w[t - 16];
    }
    std::uint32_t a = h[0];
    std::uint32_t b = h[1];
    std::uint32_t c = h[2];
    std::uint32_t d = h[3];
    std::uint32_t e = h[4];
    std::uint32_t f = h[5];
    std::uint32_t g = h[6];
    std::uint32_t hh = h[7];
    for (int t = 0; t < 64; ++t) {
        const std::uint32_t t1 = hh + sha256_big_sigma1(e)
            + sha256_choose(e, f, g) + k_sha256_k[t] + w[t];
        const std::uint32_t t2 =
            sha256_big_sigma0(a) + sha256_majority(a, b, c);
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

inline std::array<std::byte, 32> sha256(std::span<const std::byte> data) {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                          0xa54ff53a, 0x510e527f, 0x9b05688c,
                          0x1f83d9ab, 0x5be0cd19};
    std::size_t seen = 0;
    while (seen + 64 <= data.size()) {
        sha256_compress(h, data.data() + seen);
        seen += 64;
    }
    std::byte tail[128] = {};
    const std::size_t leftover = data.size() - seen;
    for (std::size_t i = 0; i < leftover; ++i) {
        tail[i] = data[seen + i];
    }
    tail[leftover] = std::byte{0x80};
    const std::size_t room = (leftover < 56) ? 64 : 128;
    const std::uint64_t bit_total =
        static_cast<std::uint64_t>(data.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[room - 8 + i] =
            static_cast<std::byte>((bit_total >> (56 - 8 * i)) & 0xff);
    }
    sha256_compress(h, tail);
    if (room == 128) sha256_compress(h, tail + 64);

    std::array<std::byte, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        sha256_store_word(digest.data() + i * 4, h[i]);
    }
    return digest;
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_SHA256_HPP_
