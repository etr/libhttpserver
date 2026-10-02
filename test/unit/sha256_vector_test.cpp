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

// TASK-114 step 3: the in-tree SHA-256 hash (FIPS 180-4 / RFC 6234)
// required by the documented SHA-256 Digest auth algorithm. The suite
// pins the published known-answer vectors (empty message, one partial
// block, the 56-byte two-block case whose padding spills into a second
// block, the 80-byte multi-block case, and the million-'a' stress
// vector).

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/detail/sha256.hpp>

#include "./littletest.hpp"

using httpserver::detail::sha256;

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

LT_BEGIN_SUITE(sha256_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(sha256_suite)

LT_BEGIN_AUTO_TEST(sha256_suite, rfc6234_known_answers)
    const struct {
        const char* message;
        const char* digest;
    } vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
         "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    };
    for (const auto& v : vectors) {
        LT_CHECK(to_hex(sha256(bytes_of(v.message))) == v.digest);
    }
LT_END_AUTO_TEST(rfc6234_known_answers)

LT_BEGIN_AUTO_TEST(sha256_suite, million_a_stress_vector)
    const std::string million(1000000, 'a');
    LT_CHECK(to_hex(sha256(bytes_of(million)))
             == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
LT_END_AUTO_TEST(million_a_stress_vector)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
