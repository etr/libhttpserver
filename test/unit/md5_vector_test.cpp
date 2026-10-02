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

// TASK-114 step 3: the in-tree MD5 hash (RFC 1321) required by the
// documented Digest auth algorithms. The suite pins all seven
// known-answer vectors of RFC 1321 section A.5, including the
// 80-digit multi-block stress case.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/detail/md5.hpp>

#include "./littletest.hpp"

using httpserver::detail::md5;

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

LT_BEGIN_SUITE(md5_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(md5_suite)

LT_BEGIN_AUTO_TEST(md5_suite, rfc1321_known_answers)
    const struct {
        const char* message;
        const char* digest;
    } vectors[] = {
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"a", "0cc175b9c0f1b6a831c399e269772661"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"abcdefghijklmnopqrstuvwxyz",
         "c3fcd3d76192e4007dfb496cca67e13b"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
         "abcdefghijklmnopqrstuvwxyz0123456789",
         "d174ab98d277d9f5a5611c2c9f419d9f"},
        {"123456789012345678901234567890123456789012345678901234567890"
         "12345678901234567890",
         "57edf4a22be3c955ac49da2e2107b67a"},
    };
    for (const auto& v : vectors) {
        LT_CHECK(to_hex(md5(bytes_of(v.message))) == v.digest);
    }
LT_END_AUTO_TEST(rfc1321_known_answers)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
