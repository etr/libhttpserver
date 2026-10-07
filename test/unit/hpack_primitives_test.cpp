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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <new>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include <httpserver/detail/hpack_primitives.hpp>
#include <httpserver/detail/hpack_static_table.hpp>
#include "./littletest.hpp"

// Task-local operator-new observer sees actual library output allocation. Test
// data and assertions are prepared outside the observed region.
namespace allocation_observer {
bool enabled = false;
std::size_t calls = 0;
std::size_t max_requested = 0;
}
void* operator new(std::size_t size) {
    if (allocation_observer::enabled) {
        ++allocation_observer::calls;
        allocation_observer::max_requested = std::max(allocation_observer::max_requested, size);
    }
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
using httpserver::detail::hpack_decode_huffman;
using httpserver::detail::hpack_decode_integer;
using httpserver::detail::hpack_decode_string;
using httpserver::detail::hpack_encode_huffman;
using httpserver::detail::hpack_encode_integer;
using httpserver::detail::hpack_encode_string;
using httpserver::detail::hpack_state;
using httpserver::detail::hpack_static_find;
using httpserver::detail::hpack_static_find_name;
using httpserver::detail::hpack_static_lookup;
using std::string_view_literals::operator""sv;
std::span<const std::uint8_t> octets(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}
}  // namespace
LT_BEGIN_SUITE(hpack_primitives_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hpack_primitives_suite)
LT_BEGIN_AUTO_TEST(hpack_primitives_suite, rfc_integers_and_boundaries)
    LT_CHECK(hpack_encode_integer(10, 5, 0, {}, 11).value == std::string("\x0a", 1));
    LT_CHECK(hpack_encode_integer(1337, 5, 0, {}, 11).value == std::string("\x1f\x9a\x0a", 3));
    LT_CHECK(hpack_encode_integer(42, 8, 0, {}, 11).value == "*");
    for (unsigned prefix = 1; prefix <= 8; ++prefix) {
        const std::uint64_t mask = (1U << prefix) - 1;
        for (auto value : {std::uint64_t{0}, mask - 1, mask, mask + 1, UINT64_MAX}) {
            const auto pattern = static_cast<std::uint8_t>(0xffU & ~mask);
            const auto encoded = hpack_encode_integer(value, prefix, pattern, {}, 11);
            LT_CHECK(encoded.status.ok());
            LT_CHECK((static_cast<std::uint8_t>(encoded.value[0]) & ~mask) == pattern);
            const auto decoded = hpack_decode_integer(octets(encoded.value + "sentinel"), prefix, {});
            LT_CHECK(decoded.status.ok());
            LT_CHECK(decoded.value == value);
            LT_CHECK(decoded.consumed == encoded.value.size());
            for (std::size_t n = 0; n < encoded.value.size(); ++n) {
                const auto partial = hpack_decode_integer(octets(encoded.value).first(n), prefix, {});
                LT_CHECK(partial.status.state == hpack_state::incomplete);
                LT_CHECK(partial.value == 0 && partial.consumed == 0);
            }
        }
    }
LT_END_AUTO_TEST(rfc_integers_and_boundaries)

LT_BEGIN_AUTO_TEST(hpack_primitives_suite, integer_rejections_and_nonminimal)
    LT_CHECK(hpack_decode_integer({}, 0, {}).status.code() == httpserver::http::outcome_code::invalid_argument);
    LT_CHECK(hpack_decode_integer({}, 9, {}).status.state == hpack_state::invalid_argument);
    LT_CHECK(hpack_decode_integer({}, 1, {10, 0}).status.state == hpack_state::invalid_argument);
    LT_CHECK(hpack_encode_integer(0, 5, 1, {}, 11).status.state == hpack_state::invalid_argument);
    LT_CHECK(hpack_encode_integer(0, 0, 0, {}, 11).status.state == hpack_state::invalid_argument);
    LT_CHECK(hpack_encode_integer(0, 5, 0, {}, 0).status.state == hpack_state::limit_exceeded);
    LT_CHECK(hpack_decode_integer(octets("\x1f\x80\0"sv), 5, {}).value == 31);
    LT_CHECK(hpack_decode_integer(octets("\x1f\x80\0"sv), 5, {31, 3}).status.ok());
    LT_CHECK(hpack_decode_integer(octets("\x1f\x80\0"sv), 5, {31, 2}).status.state == hpack_state::limit_exceeded);
    LT_CHECK(hpack_decode_integer(octets("\x1f\0"sv), 5, {30, 11}).status.state == hpack_state::limit_exceeded);
    LT_CHECK(hpack_decode_integer(octets("\x1f\x01"sv), 5, {31, 11}).status.state == hpack_state::limit_exceeded);
    LT_CHECK(hpack_encode_integer(31, 5, 0, {31, 2}, 2).status.ok());
    LT_CHECK(hpack_encode_integer(31, 5, 0, {31, 1}, 2).status.state == hpack_state::limit_exceeded);
    LT_CHECK(hpack_encode_integer(32, 5, 0, {31, 11}, 11).status.state == hpack_state::limit_exceeded);
    std::string overflow(11, '\xff');
    overflow.back() = '\x02';
    LT_CHECK(hpack_decode_integer(octets(overflow), 8, {}).status.state == hpack_state::malformed);
    overflow.back() = '\x82';
    LT_CHECK(hpack_decode_integer(octets(overflow), 8, {}).status.state == hpack_state::malformed);
    std::string zeros = "\x1f" + std::string(11, '\x80') + '\0';
    LT_CHECK(hpack_decode_integer(octets(zeros), 5, {}).status.state == hpack_state::limit_exceeded);
LT_END_AUTO_TEST(integer_rejections_and_nonminimal)
LT_BEGIN_AUTO_TEST(hpack_primitives_suite, huffman_octets_padding_and_limits)
    for (unsigned c = 0; c < 256; ++c) {
        const std::string plain(1, static_cast<char>(c));
        const auto encoded = hpack_encode_huffman(octets(plain), {4, 1}, 4);
        LT_CHECK(encoded.status.ok());
        const auto decoded = hpack_decode_huffman(octets(encoded.value), {encoded.value.size(), 1});
        LT_CHECK(decoded.status.ok() && decoded.value == plain);
        LT_CHECK(decoded.consumed == encoded.value.size());
    }
    // 'a' has a five-bit code; these eight lengths cover padding 0 through 7.
    for (std::size_t n = 1; n <= 8; ++n) {
        const std::string plain(n, 'a');
        const auto encoded = hpack_encode_huffman(octets(plain), {100, n}, 100);
        LT_CHECK(hpack_decode_huffman(octets(encoded.value), {100, n}).value == plain);
        LT_CHECK(hpack_decode_huffman(octets(encoded.value), {100, n - 1}).status.state == hpack_state::limit_exceeded);
        LT_CHECK(hpack_encode_huffman(octets(plain), {encoded.value.size(), n}, encoded.value.size()).status.ok());
        LT_CHECK(hpack_encode_huffman(octets(plain), {encoded.value.size() - 1, n}, 100).status.state == hpack_state::limit_exceeded);
        LT_CHECK(hpack_encode_huffman(octets(plain), {100, n}, encoded.value.size() - 1).status.state == hpack_state::limit_exceeded);
        LT_CHECK(hpack_encode_huffman(octets(plain), {100, n - 1}, 100).status.state == hpack_state::limit_exceeded);
    }
    LT_CHECK(hpack_decode_huffman({}, {0, 0}).status.ok());
    LT_CHECK(hpack_encode_huffman({}, {0, 0}, 0).status.ok());
    // '&' is exactly an octet (11111000), with no padding.
    LT_CHECK(hpack_encode_huffman(octets("&"), {1, 1}, 1).value == "\xf8");
    for (auto malformed : {"\xff"sv, "\xff\xff"sv, "\xff\xff\xff\xff"sv,
                           "\x1e"sv, "\x00"sv, "\x1f\xff"sv, "\xff\xff\xff\xfc\x1f"sv}) {
        const auto result = hpack_decode_huffman(octets(malformed), {100, 100});
        LT_CHECK(result.status.state == hpack_state::malformed);
        LT_CHECK(result.value.empty() && result.consumed == 0);
    }
LT_END_AUTO_TEST(huffman_octets_padding_and_limits)

LT_BEGIN_AUTO_TEST(hpack_primitives_suite, raw_and_huffman_literal_framing)
    const std::string plain = std::string("nul\0", 4) + "\xff";
    for (bool huffman : {false, true}) {
        const auto encoded = hpack_encode_string(octets(plain), huffman, {100, plain.size()}, 100);
        LT_CHECK(encoded.status.ok());
        const auto decoded = hpack_decode_string(octets(encoded.value + "residue"), {100, plain.size()});
        LT_CHECK(decoded.status.ok() && decoded.value == plain);
        LT_CHECK(decoded.consumed == encoded.value.size());
        for (std::size_t n = 0; n < encoded.value.size(); ++n) {
            const auto partial = hpack_decode_string(octets(encoded.value).first(n), {100, 100});
            LT_CHECK(partial.status.state == hpack_state::incomplete);
            LT_CHECK(partial.value.empty() && partial.consumed == 0);
        }
        LT_CHECK(hpack_decode_string(octets(encoded.value), {100, plain.size() - 1}).status.state == hpack_state::limit_exceeded);
        const auto payload = encoded.value.size() - 1;
        LT_CHECK(hpack_decode_string(octets(encoded.value), {payload, plain.size()}).status.ok());
        LT_CHECK(hpack_decode_string(octets(encoded.value), {payload - 1, plain.size()}).status.state == hpack_state::limit_exceeded);
        LT_CHECK(hpack_encode_string(octets(plain), huffman, {payload, plain.size()}, encoded.value.size()).status.ok());
        LT_CHECK(hpack_encode_string(octets(plain), huffman, {100, plain.size()}, encoded.value.size() - 1).status.state == hpack_state::limit_exceeded);
        LT_CHECK(hpack_encode_string(octets(plain), huffman, {payload - 1, plain.size()}, 100).status.state == hpack_state::limit_exceeded);
        LT_CHECK(hpack_encode_string({}, huffman, {0, 0}, 1).status.ok());
        LT_CHECK(hpack_encode_string({}, huffman, {0, 0}, 0).status.state == hpack_state::limit_exceeded);
        const auto empty = hpack_encode_string({}, huffman, {0, 0}, 1);
        LT_CHECK(hpack_decode_string(octets(empty.value), {0, 0}).status.ok());
    }
    const auto huge = hpack_encode_integer(UINT64_MAX, 7, 0, {}, 11);
    LT_CHECK(hpack_decode_string(octets(huge.value), {SIZE_MAX, SIZE_MAX}).status.state == hpack_state::incomplete);
    LT_CHECK(hpack_decode_string(octets(huge.value), {4, 4}).status.state == hpack_state::limit_exceeded);
    LT_CHECK(hpack_decode_string(octets("\x80"sv), {0, 0}).status.ok());
    LT_CHECK(hpack_decode_string(octets("\0"sv), {0, 0}).status.ok());
LT_END_AUTO_TEST(raw_and_huffman_literal_framing)

LT_BEGIN_AUTO_TEST(hpack_primitives_suite, allocation_admission_before_output)
    const std::string large(4096, 'a');
    const auto encoded = hpack_encode_huffman(octets(large), {4096, 4096}, 4096);
    LT_CHECK(encoded.status.ok());
    const auto malformed = encoded.value + std::string(4, '\xff');
    const auto raw = hpack_encode_string(octets(large), false, {4096, 4096}, 8192);
    const auto framed = hpack_encode_string(octets(large), true, {4096, 4096}, 8192);
    const auto huge = hpack_encode_integer(UINT64_MAX, 7, 0, {}, 11);
    const auto huge_claim = hpack_encode_integer(4096, 7, 0, {}, 11);
    allocation_observer::calls = 0;
    allocation_observer::enabled = true;
    const auto invalid = hpack_decode_huffman(octets(malformed), {8192, 8192});
    const auto expanded = hpack_decode_huffman(octets(encoded.value), {4096, 4095});
    const auto wire_limit = hpack_decode_huffman(octets(encoded.value), {1, 4096});
    const auto raw_limit = hpack_decode_string(octets(raw.value), {4096, 4095});
    const auto framed_limit = hpack_decode_string(octets(framed.value), {4096, 4095});
    const auto truncated = hpack_decode_string(octets(huge_claim.value), {4096, 4096});
    const auto huge_truncated = hpack_decode_string(octets(huge.value), {SIZE_MAX, SIZE_MAX});
    const auto encoder_limit = hpack_encode_huffman(octets(large), {4096, 4096}, 1);
    const auto encoder_frame_limit = hpack_encode_string(octets(large), true, {4096, 4096}, encoded.value.size());
    const auto raw_encoder_limit = hpack_encode_string(octets(large), false, {4096, 4096}, 4096);
    for (std::uint64_t i = 0; i < 100; ++i) {
        hpack_static_lookup(i);
        hpack_static_find_name("cookie");
        hpack_static_find(":method", "GET");
    }
    allocation_observer::enabled = false;
    LT_CHECK(allocation_observer::calls == 0);
    LT_CHECK(invalid.status.state == hpack_state::malformed);
    LT_CHECK(expanded.status.state == hpack_state::limit_exceeded);
    LT_CHECK(wire_limit.status.state == hpack_state::limit_exceeded);
    LT_CHECK(raw_limit.status.state == hpack_state::limit_exceeded);
    LT_CHECK(framed_limit.status.state == hpack_state::limit_exceeded);
    LT_CHECK(truncated.status.state == hpack_state::incomplete);
    LT_CHECK(huge_truncated.status.state == hpack_state::incomplete);
    LT_CHECK(encoder_limit.status.state == hpack_state::limit_exceeded);
    LT_CHECK(encoder_frame_limit.status.state == hpack_state::limit_exceeded);
    LT_CHECK(raw_encoder_limit.status.state == hpack_state::limit_exceeded);
    allocation_observer::calls = 0;
    allocation_observer::enabled = true;
    const auto admitted = hpack_decode_huffman(octets(encoded.value), {4096, 4096});
    allocation_observer::enabled = false;
    LT_CHECK(admitted.status.ok() && admitted.value == large);
    LT_CHECK(allocation_observer::calls == 1);
    // libc++ rounds string storage to a 16-byte boundary, including NUL.
    LT_CHECK(allocation_observer::max_requested <= large.size() + 16);
LT_END_AUTO_TEST(allocation_admission_before_output)

LT_BEGIN_AUTO_TEST(hpack_primitives_suite, all_octets_and_large_literal_prefix)
    std::string all;
    for (unsigned i = 0; i < 256; ++i) all += static_cast<char>(i);
    for (bool huffman : {false, true}) {
        const auto encoded = hpack_encode_string(octets(all), huffman, {1024, 256}, 1024);
        LT_CHECK(encoded.status.ok());
        const auto length = hpack_decode_integer(octets(encoded.value), 7, {});
        LT_CHECK(length.status.ok() && length.consumed > 1);
        LT_CHECK(length.value == encoded.value.size() - length.consumed);
        const auto decoded = hpack_decode_string(octets(encoded.value + "trailing"), {1024, 256});
        LT_CHECK(decoded.status.ok() && decoded.value == all && decoded.consumed == encoded.value.size());
    }
    for (auto input : {"\xff\xff\xff"sv, "\xff\xff"sv, "\xff"sv}) {
        LT_CHECK(hpack_decode_huffman(octets(input), {100, 100}).status.state == hpack_state::malformed);
    }
LT_END_AUTO_TEST(all_octets_and_large_literal_prefix)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
