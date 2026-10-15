/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/sha256.hpp>

namespace httpserver::detail {

std::uint32_t sha256_load_word(const std::byte* p) noexcept {
    std::uint32_t word = 0;
    for (int i = 0; i < 4; ++i) {
        word = (word << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(p[i]));
    }
    return word;
}

void sha256_store_word(std::byte* p, std::uint32_t word) noexcept {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::byte>((word >> (24 - 8 * i)) & 0xff);
    }
}

void sha256_compress(std::uint32_t (&h)[8], const std::byte* block) noexcept {
    std::uint32_t w[64];
    for (int t = 0; t < 16; ++t) {
        w[t] = sha256_load_word(block + t * 4);
    }
    for (int t = 16; t < 64; ++t) {
        w[t] = sha256_sigma1(w[t - 2]) + w[t - 7] + sha256_sigma0(w[t - 15]) + w[t - 16];
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
        const std::uint32_t t1 = hh + sha256_big_sigma1(e) + sha256_choose(e, f, g) + k_sha256_k[t] + w[t];
        const std::uint32_t t2 = sha256_big_sigma0(a) + sha256_majority(a, b, c);
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

std::array<std::byte, 32> sha256(std::span<const std::byte> data) {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
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
    const std::uint64_t bit_total = static_cast<std::uint64_t>(data.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[room - 8 + i] = static_cast<std::byte>((bit_total >> (56 - 8 * i)) & 0xff);
    }
    sha256_compress(h, tail);
    if (room == 128) sha256_compress(h, tail + 64);

    std::array<std::byte, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        sha256_store_word(digest.data() + i * 4, h[i]);
    }
    return digest;
}

}  // namespace httpserver::detail
