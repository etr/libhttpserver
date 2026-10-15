/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/sha1.hpp>

namespace httpserver::detail {

void sha1_compress(std::uint32_t (&h)[5], const std::byte* block) noexcept {
    std::uint32_t w[80];
    for (int t = 0; t < 16; ++t) {
        w[t] = (static_cast<std::uint32_t>(static_cast<unsigned char>(block[t * 4])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(block[t * 4 + 1])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(block[t * 4 + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(block[t * 4 + 3]));
    }
    for (int t = 16; t < 80; ++t) {
        w[t] = sha1_rotate_left(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    }
    std::uint32_t a = h[0];
    std::uint32_t b = h[1];
    std::uint32_t c = h[2];
    std::uint32_t d = h[3];
    std::uint32_t e = h[4];
    for (int t = 0; t < 80; ++t) {
        const int group = t / 20;
        const std::uint32_t temp = sha1_rotate_left(a, 5) + k_sha1_mix[group](b, c, d) + e + k_sha1_group_k[group] + w[t];
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

std::array<std::byte, 20> sha1(std::span<const std::byte> input) {
    std::uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
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
    const std::uint64_t bits = static_cast<std::uint64_t>(input.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[staged - 1 - i] = static_cast<std::byte>((bits >> (8 * i)) & 0xff);
    }
    sha1_compress(h, tail);
    if (staged == 128) sha1_compress(h, tail + 64);

    std::array<std::byte, 20> digest{};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[i * 4 + j] = static_cast<std::byte>((h[i] >> (24 - 8 * j)) & 0xff);
        }
    }
    return digest;
}

}  // namespace httpserver::detail
