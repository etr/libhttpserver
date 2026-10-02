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

// TASK-114 step 2: the in-tree SHA-1 hash (FIPS 180-4 / RFC 6234)
// required by the RFC 6455 upgrade-handshake accept computation. The
// suite pins the published known-answer vectors (empty message, one
// partial block, the 56-byte two-block case whose padding spills into
// a second block, the 80-byte multi-block case, and the million-'a'
// stress vector) plus the RFC 6455 composition the handshake actually
// uses: base64(SHA1(key + magic GUID)) for the corpus's fixed key.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/detail/base64.hpp>
#include <httpserver/detail/sha1.hpp>

#include "./littletest.hpp"

using httpserver::detail::base64_encode;
using httpserver::detail::sha1;

namespace {

std::vector<std::byte> bytes_of(std::string_view text) {
    const auto* raw = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>(raw, raw + text.size());
}

template <std::size_t N>
std::string to_hex(const std::array<std::byte, N>& digest) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(N * 2);
    for (const std::byte b : digest) {
        const unsigned value = static_cast<unsigned>(b);
        out.push_back(digits[(value >> 4) & 0xf]);
        out.push_back(digits[value & 0xf]);
    }
    return out;
}

}  // namespace

LT_BEGIN_SUITE(sha1_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(sha1_suite)

LT_BEGIN_AUTO_TEST(sha1_suite, rfc6234_known_answers)
    const struct {
        const char* message;
        const char* digest;
    } vectors[] = {
        {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
         "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "a49b2446a02c645bf419f995b67091253a04a259"},
    };
    for (const auto& v : vectors) {
        LT_CHECK(to_hex(sha1(bytes_of(v.message))) == v.digest);
    }
LT_END_AUTO_TEST(rfc6234_known_answers)

LT_BEGIN_AUTO_TEST(sha1_suite, million_a_stress_vector)
    const std::string million(1000000, 'a');
    LT_CHECK(to_hex(sha1(bytes_of(million)))
             == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
LT_END_AUTO_TEST(million_a_stress_vector)

LT_BEGIN_AUTO_TEST(sha1_suite, rfc6455_handshake_composition)
    // The upgrade-handshake computation the corpus pins in
    // websocket.tseq: base64(SHA1(fixed key + RFC 6455 GUID)).
    const std::string composed =
        std::string("dGhlIHNhbXBsZSBub25jZQ==")
        + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    LT_CHECK(base64_encode(sha1(bytes_of(composed)))
             == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
LT_END_AUTO_TEST(rfc6455_handshake_composition)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
