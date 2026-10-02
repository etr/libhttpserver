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

// TASK-114 step 4: the constant-time comparison primitive for
// credential matching (CWE-208 mitigation). The property the suite
// can pin without a timing harness is decision equivalence: over a
// corpus of pairs the constant-time comparison agrees with ordinary
// equality on every input -- equal inputs (including embedded NULs
// and high bytes), one-byte differences at every offset, prefix
// extensions, and length mismatches.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/detail/secure_compare.hpp>

#include "./littletest.hpp"

using httpserver::detail::constant_time_equal;

LT_BEGIN_SUITE(secure_compare_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(secure_compare_suite)

LT_BEGIN_AUTO_TEST(secure_compare_suite, agrees_with_equality_on_corpus)
    const std::string corpus[] = {
        "", "a", "abc", "alice", "wonderland",
        std::string("a\0b", 3),          // embedded NUL
        std::string("\xff\xfe\xfd", 3),  // high bytes
        "the quick brown fox jumps over the lazy dog",
        std::string(256, 'x'),
    };
    for (const std::string& a : corpus) {
        LT_CHECK(constant_time_equal(a, a));
        // One-byte differences at every offset.
        for (std::size_t i = 0; i < a.size(); ++i) {
            std::string b = a;
            b[i] = static_cast<char>(b[i] ^ 0x01);
            LT_CHECK(!constant_time_equal(a, b));
        }
        // Prefix and extension mismatch by length.
        if (!a.empty()) {
            LT_CHECK(!constant_time_equal(a, a.substr(0, a.size() - 1)));
            LT_CHECK(!constant_time_equal(a.substr(0, a.size() - 1), a));
        }
        // Distinct same-length pairs across the corpus.
        for (const std::string& c : corpus) {
            if (c.size() == a.size() && c != a) {
                LT_CHECK(!constant_time_equal(a, c));
            }
        }
    }
LT_END_AUTO_TEST(agrees_with_equality_on_corpus)

LT_BEGIN_AUTO_TEST(secure_compare_suite, string_view_overloads_agree)
    const std::string_view a = "YWxpY2U6d29uZGVybGFuZA==";
    const std::string_view same(a.data(), a.size());
    const std::string_view wrong = "YWxpY2U6d29uZGVybGFuZB==";
    LT_CHECK(constant_time_equal(a, same));
    LT_CHECK(!constant_time_equal(a, wrong));
LT_END_AUTO_TEST(string_view_overloads_agree)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
