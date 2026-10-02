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

// TASK-114 step 1: the in-tree Base64 codec (RFC 4648) used by Basic
// auth credentials and the RFC 6455 upgrade handshake. The suite pins:
//   - all seven RFC 4648 section 10 test vectors, both directions;
//   - strict decoding: non-alphabet characters, embedded NUL, input
//     lengths that are not a multiple of four, padding anywhere but
//     the tail, more than two padding characters, trailing garbage
//     after padding, and non-canonical trailing bits ("Zh==" carries
//     four set bits past the decoded byte) are all rejected;
//   - decode . encode round-trips over binary octets including every
//     byte value.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/detail/base64.hpp>

#include "./littletest.hpp"

using httpserver::detail::base64_decode;
using httpserver::detail::base64_encode;

namespace {

std::vector<std::byte> bytes_of(std::string_view text) {
    const auto* raw = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>(raw, raw + text.size());
}

std::string text_of(const std::vector<std::byte>& decoded) {
    std::string out;
    out.reserve(decoded.size());
    for (const std::byte b : decoded) {
        out.push_back(static_cast<char>(b));
    }
    return out;
}

}  // namespace

LT_BEGIN_SUITE(base64_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(base64_suite)

LT_BEGIN_AUTO_TEST(base64_suite, rfc4648_vectors)
    const struct {
        const char* plain;
        const char* encoded;
    } vectors[] = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };
    for (const auto& v : vectors) {
        LT_CHECK(base64_encode(bytes_of(v.plain)) == v.encoded);
        const std::optional<std::vector<std::byte>> decoded =
            base64_decode(v.encoded);
        LT_CHECK(decoded.has_value());
        if (decoded.has_value()) {
            LT_CHECK(text_of(*decoded) == v.plain);
        }
    }
LT_END_AUTO_TEST(rfc4648_vectors)

LT_BEGIN_AUTO_TEST(base64_suite, strict_decode_rejects_malformed)
    // std::string entries so the embedded-NUL case carries its length.
    const std::string rejects[] = {
        std::string("A"),             // length not a multiple of four
        std::string("ABCDE"),         // ditto
        std::string("A B="),          // space is not an alphabet character
        std::string("A-B_"),          // ditto (url-safe alphabet: other codec)
        std::string("AB=A"),          // padding must be trailing
        std::string("A==="),          // at most two padding characters
        std::string("===="),          // ditto
        std::string("Zm9vYg=x"),      // garbage after the unpadded alphabet run
        std::string("Zm9vYg==x"),     // trailing garbage after padding
        std::string("Zm9vYg==Zg=="),  // concatenations are not canonical
        std::string("Zh=="),          // non-canonical: four bits past the octet
        std::string("Zm9="),          // non-canonical: two bits past the octet
        std::string("Zg=\0=", 5),     // embedded NUL is not an alphabet char
        std::string("Zg==\n"),        // trailing whitespace is still garbage
    };
    for (const std::string& bad : rejects) {
        const std::optional<std::vector<std::byte>> decoded =
            base64_decode(bad);
        LT_CHECK(!decoded.has_value());
    }
LT_END_AUTO_TEST(strict_decode_rejects_malformed)

LT_BEGIN_AUTO_TEST(base64_suite, round_trips_every_octet_pattern)
    std::vector<std::byte> blob;
    for (std::uint32_t i = 0; i < 512; ++i) {
        blob.push_back(static_cast<std::byte>(i & 0xff));
    }
    // Every length 0..7 over a sliding window: all three quantum tails.
    for (std::size_t len = 0; len <= 7; ++len) {
        const std::span<const std::byte> view(blob.data(), len);
        const std::string encoded = base64_encode(view);
        const std::optional<std::vector<std::byte>> decoded =
            base64_decode(encoded);
        LT_CHECK(decoded.has_value());
        if (decoded.has_value()) {
            LT_CHECK(decoded->size() == len);
            LT_CHECK(text_of(*decoded) == text_of(
                std::vector<std::byte>(view.begin(), view.end())));
        }
    }
    // The full blob round-trips as one string.
    const std::optional<std::vector<std::byte>> all =
        base64_decode(base64_encode(blob));
    LT_CHECK(all.has_value());
    if (all.has_value()) {
        LT_CHECK(*all == blob);
    }
LT_END_AUTO_TEST(round_trips_every_octet_pattern)

LT_BEGIN_AUTO_TEST(base64_suite, standard_alphabet_specials_decode)
    // '+' and '/' are ordinary standard-alphabet characters (the RFC
    // 4648 url-safe variant is a different codec and stays rejected).
    const std::optional<std::vector<std::byte>> decoded = base64_decode("A/B+");
    LT_CHECK(decoded.has_value());
    if (decoded.has_value()) {
        LT_CHECK(decoded->size() == 3);
        if (decoded->size() == 3) {
            LT_CHECK((*decoded)[0] == std::byte{0x03});
            LT_CHECK((*decoded)[1] == std::byte{0xf0});
            LT_CHECK((*decoded)[2] == std::byte{0x7e});
        }
    }
LT_END_AUTO_TEST(standard_alphabet_specials_decode)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
