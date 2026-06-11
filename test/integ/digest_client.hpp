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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#ifndef TEST_INTEG_DIGEST_CLIENT_HPP_
#define TEST_INTEG_DIGEST_CLIENT_HPP_

// TASK-079: header-only test-side RFC 7616 Digest client helper.
//
// Why it exists
// -------------
// The six v2 digest-auth integ tests (`digest_auth`, `digest_auth_wrong_pass`,
// `digest_auth_with_ha1_md5[_wrong_pass]`, `digest_auth_with_ha1_sha256
// [_wrong_pass]`, `digest_user_cache_with_auth`) previously delegated the
// nonce/opaque/qop computation to libcurl's CURLAUTH_DIGEST. libcurl never
// reveals what it received from the server or what it signed with, which
// makes the HA1-precomputed acceptance criterion ("server accepts the
// precomputed credential and does not recompute MD5/SHA-256 on the secret")
// unobservable: libcurl always hashes the cleartext password it was given
// via CURLOPT_USERPWD.
//
// This helper lets the tests *become* the digest client: parse the
// WWW-Authenticate challenge, compute the response with either a cleartext
// password OR a precomputed HA1, and ship the Authorization header
// ourselves. The HA1-precomputed wire round-trip is the behavioural proxy
// for "server validates against the configured HA1, not against a
// re-derived MD5/SHA-256 of cleartext" because the cleartext never leaves
// the test.
//
// Scope
// -----
//   - Implements MD5 (RFC 1321) and SHA-256 (FIPS 180-4) as inline,
//     public-domain reference implementations. No new build dep.
//   - Implements the qop=auth response computation per RFC 7616 §3.4.1,
//     for both cleartext-password and precomputed-HA1 paths.
//   - The library only emits non-sess algorithms (MD5, SHA-256) and only
//     qop=auth (`detail/webserver_request.cpp:362-380`), so this helper
//     intentionally does NOT implement MD5-sess / SHA-256-sess or
//     qop=auth-int. Calls with those tokens are rejected at the call site.
//   - userhash and stale-nonce re-challenge are also out of scope (no
//     consumer in the targeted tests).

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace httpserver_test {

// ---------------------------------------------------------------------------
// Inline MD5 (RFC 1321) reference implementation. Adapted from the public-
// domain implementation by Alexander Peslyak (Solar Designer), which is in
// turn derived from the RFC 1321 reference. Copy-pasted verbatim into a
// detail namespace; not modified. The self-test in
// `test/unit/digest_client_self_test.cpp` pins the FIPS-published vectors.
// ---------------------------------------------------------------------------

namespace detail {

struct md5_ctx {
    std::uint32_t lo, hi;
    std::uint32_t a, b, c, d;
    unsigned char buffer[64];
    std::uint32_t block[16];
};

#define MD5_F(x, y, z)            ((z) ^ ((x) & ((y) ^ (z))))
#define MD5_G(x, y, z)            ((y) ^ ((z) & ((x) ^ (y))))
#define MD5_H(x, y, z)            (((x) ^ (y)) ^ (z))
#define MD5_H2(x, y, z)           ((x) ^ ((y) ^ (z)))
#define MD5_I(x, y, z)            ((y) ^ ((x) | ~(z)))

#define MD5_STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (t); \
    (a) = (((a) << (s)) | (((a) & 0xffffffff) >> (32 - (s)))); \
    (a) += (b);

inline const void* md5_body(md5_ctx* ctx, const void* data, std::size_t size) {
    const unsigned char* ptr = static_cast<const unsigned char*>(data);
    std::uint32_t a, b, c, d;
    std::uint32_t saved_a, saved_b, saved_c, saved_d;

    a = ctx->a;
    b = ctx->b;
    c = ctx->c;
    d = ctx->d;

    do {
        saved_a = a;
        saved_b = b;
        saved_c = c;
        saved_d = d;

        // Pack 64 bytes little-endian into ctx->block.
        for (int i = 0; i < 16; ++i) {
            ctx->block[i] =
                  (static_cast<std::uint32_t>(ptr[i * 4 + 0]))
                | (static_cast<std::uint32_t>(ptr[i * 4 + 1]) << 8)
                | (static_cast<std::uint32_t>(ptr[i * 4 + 2]) << 16)
                | (static_cast<std::uint32_t>(ptr[i * 4 + 3]) << 24);
        }

        MD5_STEP(MD5_F, a, b, c, d, ctx->block[0],  0xd76aa478, 7)
        MD5_STEP(MD5_F, d, a, b, c, ctx->block[1],  0xe8c7b756, 12)
        MD5_STEP(MD5_F, c, d, a, b, ctx->block[2],  0x242070db, 17)
        MD5_STEP(MD5_F, b, c, d, a, ctx->block[3],  0xc1bdceee, 22)
        MD5_STEP(MD5_F, a, b, c, d, ctx->block[4],  0xf57c0faf, 7)
        MD5_STEP(MD5_F, d, a, b, c, ctx->block[5],  0x4787c62a, 12)
        MD5_STEP(MD5_F, c, d, a, b, ctx->block[6],  0xa8304613, 17)
        MD5_STEP(MD5_F, b, c, d, a, ctx->block[7],  0xfd469501, 22)
        MD5_STEP(MD5_F, a, b, c, d, ctx->block[8],  0x698098d8, 7)
        MD5_STEP(MD5_F, d, a, b, c, ctx->block[9],  0x8b44f7af, 12)
        MD5_STEP(MD5_F, c, d, a, b, ctx->block[10], 0xffff5bb1, 17)
        MD5_STEP(MD5_F, b, c, d, a, ctx->block[11], 0x895cd7be, 22)
        MD5_STEP(MD5_F, a, b, c, d, ctx->block[12], 0x6b901122, 7)
        MD5_STEP(MD5_F, d, a, b, c, ctx->block[13], 0xfd987193, 12)
        MD5_STEP(MD5_F, c, d, a, b, ctx->block[14], 0xa679438e, 17)
        MD5_STEP(MD5_F, b, c, d, a, ctx->block[15], 0x49b40821, 22)

        MD5_STEP(MD5_G, a, b, c, d, ctx->block[1],  0xf61e2562, 5)
        MD5_STEP(MD5_G, d, a, b, c, ctx->block[6],  0xc040b340, 9)
        MD5_STEP(MD5_G, c, d, a, b, ctx->block[11], 0x265e5a51, 14)
        MD5_STEP(MD5_G, b, c, d, a, ctx->block[0],  0xe9b6c7aa, 20)
        MD5_STEP(MD5_G, a, b, c, d, ctx->block[5],  0xd62f105d, 5)
        MD5_STEP(MD5_G, d, a, b, c, ctx->block[10], 0x02441453, 9)
        MD5_STEP(MD5_G, c, d, a, b, ctx->block[15], 0xd8a1e681, 14)
        MD5_STEP(MD5_G, b, c, d, a, ctx->block[4],  0xe7d3fbc8, 20)
        MD5_STEP(MD5_G, a, b, c, d, ctx->block[9],  0x21e1cde6, 5)
        MD5_STEP(MD5_G, d, a, b, c, ctx->block[14], 0xc33707d6, 9)
        MD5_STEP(MD5_G, c, d, a, b, ctx->block[3],  0xf4d50d87, 14)
        MD5_STEP(MD5_G, b, c, d, a, ctx->block[8],  0x455a14ed, 20)
        MD5_STEP(MD5_G, a, b, c, d, ctx->block[13], 0xa9e3e905, 5)
        MD5_STEP(MD5_G, d, a, b, c, ctx->block[2],  0xfcefa3f8, 9)
        MD5_STEP(MD5_G, c, d, a, b, ctx->block[7],  0x676f02d9, 14)
        MD5_STEP(MD5_G, b, c, d, a, ctx->block[12], 0x8d2a4c8a, 20)

        MD5_STEP(MD5_H,  a, b, c, d, ctx->block[5],  0xfffa3942, 4)
        MD5_STEP(MD5_H2, d, a, b, c, ctx->block[8],  0x8771f681, 11)
        MD5_STEP(MD5_H,  c, d, a, b, ctx->block[11], 0x6d9d6122, 16)
        MD5_STEP(MD5_H2, b, c, d, a, ctx->block[14], 0xfde5380c, 23)
        MD5_STEP(MD5_H,  a, b, c, d, ctx->block[1],  0xa4beea44, 4)
        MD5_STEP(MD5_H2, d, a, b, c, ctx->block[4],  0x4bdecfa9, 11)
        MD5_STEP(MD5_H,  c, d, a, b, ctx->block[7],  0xf6bb4b60, 16)
        MD5_STEP(MD5_H2, b, c, d, a, ctx->block[10], 0xbebfbc70, 23)
        MD5_STEP(MD5_H,  a, b, c, d, ctx->block[13], 0x289b7ec6, 4)
        MD5_STEP(MD5_H2, d, a, b, c, ctx->block[0],  0xeaa127fa, 11)
        MD5_STEP(MD5_H,  c, d, a, b, ctx->block[3],  0xd4ef3085, 16)
        MD5_STEP(MD5_H2, b, c, d, a, ctx->block[6],  0x04881d05, 23)
        MD5_STEP(MD5_H,  a, b, c, d, ctx->block[9],  0xd9d4d039, 4)
        MD5_STEP(MD5_H2, d, a, b, c, ctx->block[12], 0xe6db99e5, 11)
        MD5_STEP(MD5_H,  c, d, a, b, ctx->block[15], 0x1fa27cf8, 16)
        MD5_STEP(MD5_H2, b, c, d, a, ctx->block[2],  0xc4ac5665, 23)

        MD5_STEP(MD5_I, a, b, c, d, ctx->block[0],  0xf4292244, 6)
        MD5_STEP(MD5_I, d, a, b, c, ctx->block[7],  0x432aff97, 10)
        MD5_STEP(MD5_I, c, d, a, b, ctx->block[14], 0xab9423a7, 15)
        MD5_STEP(MD5_I, b, c, d, a, ctx->block[5],  0xfc93a039, 21)
        MD5_STEP(MD5_I, a, b, c, d, ctx->block[12], 0x655b59c3, 6)
        MD5_STEP(MD5_I, d, a, b, c, ctx->block[3],  0x8f0ccc92, 10)
        MD5_STEP(MD5_I, c, d, a, b, ctx->block[10], 0xffeff47d, 15)
        MD5_STEP(MD5_I, b, c, d, a, ctx->block[1],  0x85845dd1, 21)
        MD5_STEP(MD5_I, a, b, c, d, ctx->block[8],  0x6fa87e4f, 6)
        MD5_STEP(MD5_I, d, a, b, c, ctx->block[15], 0xfe2ce6e0, 10)
        MD5_STEP(MD5_I, c, d, a, b, ctx->block[6],  0xa3014314, 15)
        MD5_STEP(MD5_I, b, c, d, a, ctx->block[13], 0x4e0811a1, 21)
        MD5_STEP(MD5_I, a, b, c, d, ctx->block[4],  0xf7537e82, 6)
        MD5_STEP(MD5_I, d, a, b, c, ctx->block[11], 0xbd3af235, 10)
        MD5_STEP(MD5_I, c, d, a, b, ctx->block[2],  0x2ad7d2bb, 15)
        MD5_STEP(MD5_I, b, c, d, a, ctx->block[9],  0xeb86d391, 21)

        a += saved_a;
        b += saved_b;
        c += saved_c;
        d += saved_d;

        ptr += 64;
    } while (size -= 64);

    ctx->a = a;
    ctx->b = b;
    ctx->c = c;
    ctx->d = d;

    return ptr;
}

inline void md5_init(md5_ctx* ctx) {
    ctx->a = 0x67452301;
    ctx->b = 0xefcdab89;
    ctx->c = 0x98badcfe;
    ctx->d = 0x10325476;
    ctx->lo = 0;
    ctx->hi = 0;
}

inline void md5_update(md5_ctx* ctx, const void* data, std::size_t size) {
    std::uint32_t saved_lo = ctx->lo;
    if ((ctx->lo = (saved_lo + size) & 0x1fffffff) < saved_lo) {
        ctx->hi++;
    }
    ctx->hi += static_cast<std::uint32_t>(size >> 29);

    std::size_t used = saved_lo & 0x3f;
    if (used) {
        std::size_t available = 64 - used;
        if (size < available) {
            std::memcpy(&ctx->buffer[used], data, size);
            return;
        }
        std::memcpy(&ctx->buffer[used], data, available);
        data = static_cast<const unsigned char*>(data) + available;
        size -= available;
        md5_body(ctx, ctx->buffer, 64);
    }
    if (size >= 64) {
        data = md5_body(ctx, data, size & ~static_cast<std::size_t>(0x3f));
        size &= 0x3f;
    }
    std::memcpy(ctx->buffer, data, size);
}

#define MD5_OUT(dst, src) \
    (dst)[0] = static_cast<unsigned char>(src); \
    (dst)[1] = static_cast<unsigned char>((src) >> 8); \
    (dst)[2] = static_cast<unsigned char>((src) >> 16); \
    (dst)[3] = static_cast<unsigned char>((src) >> 24);

inline void md5_final(md5_ctx* ctx, unsigned char* result) {
    std::size_t used = ctx->lo & 0x3f;
    ctx->buffer[used++] = 0x80;
    std::size_t available = 64 - used;
    if (available < 8) {
        std::memset(&ctx->buffer[used], 0, available);
        md5_body(ctx, ctx->buffer, 64);
        used = 0;
        available = 64;
    }
    std::memset(&ctx->buffer[used], 0, available - 8);
    ctx->lo <<= 3;
    MD5_OUT(&ctx->buffer[56], ctx->lo)
    MD5_OUT(&ctx->buffer[60], ctx->hi)
    md5_body(ctx, ctx->buffer, 64);

    MD5_OUT(&result[0],  ctx->a)
    MD5_OUT(&result[4],  ctx->b)
    MD5_OUT(&result[8],  ctx->c)
    MD5_OUT(&result[12], ctx->d)

    std::memset(ctx, 0, sizeof(*ctx));
}

#undef MD5_F
#undef MD5_G
#undef MD5_H
#undef MD5_H2
#undef MD5_I
#undef MD5_STEP
#undef MD5_OUT

// One-shot MD5: produces 16 bytes at `out`.
inline void md5(const unsigned char* data, std::size_t n, unsigned char out[16]) {
    md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, n);
    md5_final(&ctx, out);
}

inline void md5(std::string_view sv, unsigned char out[16]) {
    md5(reinterpret_cast<const unsigned char*>(sv.data()), sv.size(), out);
}

// ---------------------------------------------------------------------------
// Inline SHA-256 (FIPS 180-4) reference implementation. Adapted from the
// public-domain implementation by Brad Conte
// (https://github.com/B-Con/crypto-algorithms, public domain). Copy-pasted
// verbatim into the detail namespace; not modified. The self-test pins the
// FIPS canonical vectors.
// ---------------------------------------------------------------------------

struct sha256_ctx {
    unsigned char data[64];
    std::uint32_t datalen;
    std::uint64_t bitlen;
    std::uint32_t state[8];
};

#define SHA256_ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define SHA256_CH(x, y, z)    (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z)   (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x)         (SHA256_ROTRIGHT(x, 2) ^ SHA256_ROTRIGHT(x, 13) ^ SHA256_ROTRIGHT(x, 22))
#define SHA256_EP1(x)         (SHA256_ROTRIGHT(x, 6) ^ SHA256_ROTRIGHT(x, 11) ^ SHA256_ROTRIGHT(x, 25))
#define SHA256_SIG0(x)        (SHA256_ROTRIGHT(x, 7) ^ SHA256_ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x)        (SHA256_ROTRIGHT(x, 17) ^ SHA256_ROTRIGHT(x, 19) ^ ((x) >> 10))

inline const std::uint32_t* sha256_k() {
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    return k;
}

inline void sha256_transform(sha256_ctx* ctx, const unsigned char data[64]) {
    std::uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    const std::uint32_t* k = sha256_k();

    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = (static_cast<std::uint32_t>(data[j]) << 24)
             | (static_cast<std::uint32_t>(data[j + 1]) << 16)
             | (static_cast<std::uint32_t>(data[j + 2]) << 8)
             | (static_cast<std::uint32_t>(data[j + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + k[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

inline void sha256_init(sha256_ctx* ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

inline void sha256_update(sha256_ctx* ctx, const unsigned char* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

inline void sha256_final(sha256_ctx* ctx, unsigned char hash[32]) {
    std::uint32_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        std::memset(ctx->data, 0, 56);
    }

    ctx->bitlen += static_cast<std::uint64_t>(ctx->datalen) * 8;
    ctx->data[63] = static_cast<unsigned char>(ctx->bitlen);
    ctx->data[62] = static_cast<unsigned char>(ctx->bitlen >> 8);
    ctx->data[61] = static_cast<unsigned char>(ctx->bitlen >> 16);
    ctx->data[60] = static_cast<unsigned char>(ctx->bitlen >> 24);
    ctx->data[59] = static_cast<unsigned char>(ctx->bitlen >> 32);
    ctx->data[58] = static_cast<unsigned char>(ctx->bitlen >> 40);
    ctx->data[57] = static_cast<unsigned char>(ctx->bitlen >> 48);
    ctx->data[56] = static_cast<unsigned char>(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        for (std::uint32_t j = 0; j < 8; ++j) {
            hash[i + j * 4] = static_cast<unsigned char>(
                (ctx->state[j] >> (24 - i * 8)) & 0x000000ff);
        }
    }
}

#undef SHA256_ROTRIGHT
#undef SHA256_CH
#undef SHA256_MAJ
#undef SHA256_EP0
#undef SHA256_EP1
#undef SHA256_SIG0
#undef SHA256_SIG1

inline void sha256(const unsigned char* data, std::size_t n, unsigned char out[32]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, n);
    sha256_final(&ctx, out);
}

inline void sha256(std::string_view sv, unsigned char out[32]) {
    sha256(reinterpret_cast<const unsigned char*>(sv.data()), sv.size(), out);
}

// Hex-encode `n` bytes from `bytes` as a lowercase hex string.
inline std::string to_hex_lower(const unsigned char* bytes, std::size_t n) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(digits[(bytes[i] >> 4) & 0xF]);
        out.push_back(digits[bytes[i] & 0xF]);
    }
    return out;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

struct parsed_challenge {
    std::string realm;
    std::string nonce;
    std::string opaque;       // may be empty (RFC 7616 §3.3 RECOMMENDED)
    std::string algorithm;    // "MD5" or "SHA-256" (verbatim from server)
    std::string qop;          // "auth" (only supported value)
    bool        stale = false;
};

enum class digest_hash { md5, sha256 };

namespace digest_client_internal {

// Skip whitespace including tabs.
inline void skip_ws(std::string_view& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
}

// Compare prefix case-insensitively.
inline bool ieq_prefix(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

// Parse one auth-param: NAME=VALUE or NAME="QUOTED". Returns name/value via
// out params and advances `s` past the param (and any trailing comma/space).
inline bool parse_one_param(std::string_view& s,
                            std::string& name,
                            std::string& value) {
    skip_ws(s);
    if (s.empty()) return false;

    // Read name (token).
    std::size_t name_end = 0;
    while (name_end < s.size() && s[name_end] != '=' &&
           s[name_end] != ' ' && s[name_end] != '\t' && s[name_end] != ',') {
        ++name_end;
    }
    if (name_end == 0) return false;
    name.assign(s.data(), name_end);
    s.remove_prefix(name_end);

    skip_ws(s);
    if (s.empty() || s.front() != '=') {
        // Missing value: treat as a bare token (e.g., stale).
        value.clear();
        // Skip trailing comma if present.
        if (!s.empty() && s.front() == ',') s.remove_prefix(1);
        return true;
    }
    s.remove_prefix(1);  // consume '='
    skip_ws(s);

    value.clear();
    if (!s.empty() && s.front() == '"') {
        // Quoted string per RFC 7235 / RFC 7230 §3.2.6.
        s.remove_prefix(1);
        while (!s.empty() && s.front() != '"') {
            if (s.front() == '\\' && s.size() >= 2) {
                value.push_back(s[1]);
                s.remove_prefix(2);
            } else {
                value.push_back(s.front());
                s.remove_prefix(1);
            }
        }
        if (!s.empty() && s.front() == '"') s.remove_prefix(1);
    } else {
        // Unquoted token: read until comma or whitespace.
        std::size_t v_end = 0;
        while (v_end < s.size() && s[v_end] != ',' &&
               s[v_end] != ' ' && s[v_end] != '\t') {
            ++v_end;
        }
        value.assign(s.data(), v_end);
        s.remove_prefix(v_end);
    }

    skip_ws(s);
    if (!s.empty() && s.front() == ',') s.remove_prefix(1);
    return true;
}

// Dispatch the chosen H() over `input`.
inline std::string H_hex(digest_hash hash, std::string_view input) {
    if (hash == digest_hash::md5) {
        unsigned char out[16];
        detail::md5(input, out);
        return detail::to_hex_lower(out, 16);
    }
    unsigned char out[32];
    detail::sha256(input, out);
    return detail::to_hex_lower(out, 32);
}

// Concatenate the RFC 7616 §3.4.1 response chain:
//   response = H( HA1 ":" nonce ":" nc ":" cnonce ":" qop ":" HA2 )
inline std::string assemble_response(digest_hash hash,
                                     std::string_view ha1_hex,
                                     std::string_view nonce,
                                     std::string_view nc,
                                     std::string_view cnonce,
                                     std::string_view qop,
                                     std::string_view ha2_hex) {
    std::string buf;
    buf.reserve(ha1_hex.size() + nonce.size() + nc.size() +
                cnonce.size() + qop.size() + ha2_hex.size() + 5);
    buf.append(ha1_hex);
    buf.push_back(':');
    buf.append(nonce);
    buf.push_back(':');
    buf.append(nc);
    buf.push_back(':');
    buf.append(cnonce);
    buf.push_back(':');
    buf.append(qop);
    buf.push_back(':');
    buf.append(ha2_hex);
    return H_hex(hash, buf);
}

// Shared A2 construction + response assembly used by both
// compute_response_cleartext (which derives ha1_hex from cleartext) and
// compute_response_ha1 (which hex-encodes a precomputed raw HA1).
inline std::string compute_response_from_ha1_hex(
        digest_hash hash,
        std::string_view ha1_hex,
        const parsed_challenge& ch,
        std::string_view method,
        std::string_view request_uri,
        std::string_view cnonce,
        std::string_view nc) {
    // A2 = Method ":" request-uri
    std::string a2;
    a2.reserve(method.size() + request_uri.size() + 1);
    a2.append(method);
    a2.push_back(':');
    a2.append(request_uri);
    std::string ha2 = H_hex(hash, a2);
    return assemble_response(hash, ha1_hex, ch.nonce, nc, cnonce, ch.qop, ha2);
}

}  // namespace digest_client_internal

// Parse the value of a `WWW-Authenticate:` Digest challenge header (the
// part after "Digest "). Lenient about whitespace and unquoted vs. quoted
// values per RFC 7616 §3.3 / RFC 7235 §2.1. Returns std::nullopt when the
// scheme is not "Digest" or when a mandatory field (realm, nonce) is missing.
inline std::optional<parsed_challenge> parse_www_authenticate(std::string_view raw) {
    using digest_client_internal::ieq_prefix;
    using digest_client_internal::skip_ws;
    using digest_client_internal::parse_one_param;

    skip_ws(raw);
    if (ieq_prefix(raw, "Digest")) {
        raw.remove_prefix(std::strlen("Digest"));
    } else {
        return std::nullopt;
    }
    skip_ws(raw);

    parsed_challenge out;
    std::string name, value;
    while (parse_one_param(raw, name, value)) {
        // Compare names case-insensitively.
        auto ieq = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        };
        if (ieq(name, "realm"))          out.realm = value;
        else if (ieq(name, "nonce"))     out.nonce = value;
        else if (ieq(name, "opaque"))    out.opaque = value;
        else if (ieq(name, "algorithm")) out.algorithm = value;
        else if (ieq(name, "qop"))       out.qop = value;
        else if (ieq(name, "stale"))     out.stale =
            ieq(value, "true") || ieq(value, "TRUE");
        // Unknown params: silently ignored per RFC 7616 §3.3.
        if (raw.empty()) break;
    }

    if (out.realm.empty() || out.nonce.empty()) return std::nullopt;
    if (out.qop.empty()) out.qop = "auth";  // RFC 7616 §3.3 default
    // qop may arrive as a list (e.g. `auth,auth-int`); take the first token.
    auto comma = out.qop.find(',');
    if (comma != std::string::npos) out.qop.resize(comma);
    return out;
}

// Scan a raw header block captured by CURLOPT_HEADERFUNCTION and return the
// first "WWW-Authenticate: Digest …" challenge found.
inline std::optional<parsed_challenge> extract_digest_challenge(std::string_view headers) {
    using digest_client_internal::ieq_prefix;

    while (!headers.empty()) {
        // Find end of current line.
        auto line_end = headers.find("\r\n");
        std::string_view line = (line_end == std::string_view::npos)
            ? headers
            : headers.substr(0, line_end);
        std::string_view rest = (line_end == std::string_view::npos)
            ? std::string_view{}
            : headers.substr(line_end + 2);

        if (ieq_prefix(line, "WWW-Authenticate:")) {
            line.remove_prefix(std::strlen("WWW-Authenticate:"));
            // Strip leading whitespace.
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.remove_prefix(1);
            }
            auto parsed = parse_www_authenticate(line);
            if (parsed) return parsed;
            // Skip non-Digest WWW-Authenticate headers and keep scanning.
        }
        headers = rest;
    }
    return std::nullopt;
}

// Compute the RFC 7616 §3.4 `response=` token using a cleartext password.
inline std::string compute_response_cleartext(
        const parsed_challenge& ch,
        digest_hash hash,
        std::string_view method,
        std::string_view request_uri,
        std::string_view username,
        std::string_view password,
        std::string_view cnonce,
        std::string_view nc) {
    // A1 = unq(username) ":" unq(realm) ":" passwd
    std::string a1;
    a1.reserve(username.size() + ch.realm.size() + password.size() + 2);
    a1.append(username);
    a1.push_back(':');
    a1.append(ch.realm);
    a1.push_back(':');
    a1.append(password);
    std::string ha1_hex = digest_client_internal::H_hex(hash, a1);
    return digest_client_internal::compute_response_from_ha1_hex(
        hash, ha1_hex, ch, method, request_uri, cnonce, nc);
}

// Compute the RFC 7616 §3.4 `response=` token from a precomputed HA1.
inline std::string compute_response_ha1(
        const parsed_challenge& ch,
        digest_hash hash,
        std::string_view method,
        std::string_view request_uri,
        const unsigned char* ha1,
        std::size_t ha1_size,
        std::string_view cnonce,
        std::string_view nc) {
    std::string ha1_hex = detail::to_hex_lower(ha1, ha1_size);
    return digest_client_internal::compute_response_from_ha1_hex(
        hash, ha1_hex, ch, method, request_uri, cnonce, nc);
}

// Serialize a full `Authorization: Digest …` header value (the part after
// "Authorization: ") per RFC 7616 §3.4. Quotes every field RFC 7616 says
// MUST be quoted; emits algorithm and qop verbatim (unquoted) per the BNF.
inline std::string build_authorization_header(
        const parsed_challenge& ch,
        std::string_view username,
        std::string_view request_uri,
        std::string_view cnonce,
        std::string_view nc,
        std::string_view response) {
    std::string out;
    out.reserve(256);
    out.append("Digest ");
    out.append("username=\"").append(username).append("\", ");
    out.append("realm=\"").append(ch.realm).append("\", ");
    out.append("nonce=\"").append(ch.nonce).append("\", ");
    out.append("uri=\"").append(request_uri).append("\", ");
    if (!ch.algorithm.empty()) {
        // RFC 7616 §3.4: algorithm is an unquoted token.
        out.append("algorithm=").append(ch.algorithm).append(", ");
    }
    out.append("qop=").append(ch.qop).append(", ");
    out.append("nc=").append(nc).append(", ");
    out.append("cnonce=\"").append(cnonce).append("\", ");
    out.append("response=\"").append(response).append("\"");
    if (!ch.opaque.empty()) {
        out.append(", opaque=\"").append(ch.opaque).append("\"");
    }
    return out;
}

// Generate a 32-hex-char client nonce (16 random bytes) from a fresh random
// device. Test-only; not cryptographically graded but ample for handshake
// replay uniqueness in a single test run.
inline std::string make_cnonce() {
    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uint64_t hi = rng();
    std::uint64_t lo = rng();
    unsigned char buf[16];
    std::memcpy(buf,     &hi, 8);
    std::memcpy(buf + 8, &lo, 8);
    return detail::to_hex_lower(buf, 16);
}

}  // namespace httpserver_test

#endif  // TEST_INTEG_DIGEST_CLIENT_HPP_
