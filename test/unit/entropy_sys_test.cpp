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

// TASK-114 step 4: the OS randomness seam (entropy_sys.hpp) that
// Digest nonce generation will draw from (TASK-115). The suite pins
// the observable contract: zero-length fills succeed, fills honor
// the requested length and report ok, successive fills differ, a
// batch of fills is not degenerate (not all identical, not all zero,
// and not all repeated copies of one pattern), and a 4096-byte fill
// exercises whatever chunking the platform branch performs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <httpserver/detail/entropy_sys.hpp>
#include <httpserver/http/outcome.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
namespace detail = httpserver::detail;

bool all_zero(const std::vector<std::byte>& blob) {
    for (const std::byte b : blob) {
        if (b != std::byte{0}) return false;
    }
    return true;
}

std::vector<std::byte> drawn(std::size_t length) {
    std::vector<std::byte> blob(length, std::byte{0});
    const http::outcome filled = detail::entropy::fill(blob);
    if (!filled.ok()) return {};
    return blob;
}

}  // namespace

LT_BEGIN_SUITE(entropy_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(entropy_suite)

LT_BEGIN_AUTO_TEST(entropy_suite, zero_length_fill_succeeds)
    std::vector<std::byte> none;
    LT_CHECK(detail::entropy::fill(none).ok());
LT_END_AUTO_TEST(zero_length_fill_succeeds)

LT_BEGIN_AUTO_TEST(entropy_suite, fill_honors_length_and_reports_ok)
    for (const std::size_t length : {std::size_t{1}, std::size_t{16},
                                     std::size_t{32}, std::size_t{64},
                                     std::size_t{257}, std::size_t{4096}}) {
        const std::vector<std::byte> blob = drawn(length);
        LT_CHECK(blob.size() == length);
        if (blob.size() == length) {
            LT_CHECK(!all_zero(blob));
        }
    }
LT_END_AUTO_TEST(fill_honors_length_and_reports_ok)

LT_BEGIN_AUTO_TEST(entropy_suite, successive_fills_differ)
    const std::vector<std::byte> first = drawn(32);
    const std::vector<std::byte> second = drawn(32);
    LT_CHECK(first.size() == 32);
    LT_CHECK(second.size() == 32);
    LT_CHECK(first != second);
LT_END_AUTO_TEST(successive_fills_differ)

LT_BEGIN_AUTO_TEST(entropy_suite, batch_of_fills_not_degenerate)
    std::vector<std::vector<std::byte>> batch;
    for (int i = 0; i < 256; ++i) {
        batch.push_back(drawn(16));
    }
    std::size_t distinct = 0;
    std::size_t nonzero = 0;
    for (std::size_t i = 0; i < batch.size(); ++i) {
        if (!all_zero(batch[i])) ++nonzero;
        for (std::size_t j = 0; j < i; ++j) {
            if (batch[i] == batch[j]) {
                ++distinct;  // a duplicate pair
                break;
            }
        }
    }
    // All 256 sixteen-byte fills identical would be a catastrophic
    // generator; all zero is the degenerate case the seam rejects.
    LT_CHECK(batch.size() == 256);
    LT_CHECK(nonzero == 256);
    LT_CHECK(distinct == 0);
LT_END_AUTO_TEST(batch_of_fills_not_degenerate)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
