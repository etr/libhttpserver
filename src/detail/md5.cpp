/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/md5.hpp>

namespace httpserver::detail {

void md5_compress(std::uint32_t (&abcd)[4], const std::byte* block) noexcept {
    std::uint32_t x[16];
    for (int j = 0; j < 16; ++j) {
        x[j] = static_cast<std::uint32_t>(static_cast<unsigned char>(block[j * 4])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(block[j * 4 + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(block[j * 4 + 2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(block[j * 4 + 3])) << 24);
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
        const std::uint32_t stepped = md5_step(a, mixed, b, x[k], t, s);
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

std::array<std::byte, 16> md5(std::span<const std::byte> message) {
    std::uint32_t abcd[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
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
    const std::uint64_t bit_count = static_cast<std::uint64_t>(message.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[total - 8 + i] = static_cast<std::byte>((bit_count >> (8 * i)) & 0xff);
    }
    md5_compress(abcd, tail);
    if (total == 128) md5_compress(abcd, tail + 64);

    std::array<std::byte, 16> digest{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[i * 4 + j] = static_cast<std::byte>((abcd[i] >> (8 * j)) & 0xff);
        }
    }
    return digest;
}

}  // namespace httpserver::detail
