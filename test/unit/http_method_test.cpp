/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

// Compile-time and runtime verification of httpserver::http_method and
// httpserver::method_set. Drives both acceptance-criteria asserts plus
// layout / width pinning, bitwise composition, complement bounding,
// to_string totality, and round-trip via set/contains.

#include <microhttpd.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// AC #1 — set/contains round-trip in constant context.
static_assert(httpserver::method_set{}.set(httpserver::http_method::get)
                 .contains(httpserver::http_method::get),
              "method_set::set followed by contains must hold at compile time");

// AC #2 — bitmask width sanity.
static_assert(static_cast<std::uint8_t>(httpserver::http_method::count_) <= 32,
              "http_method::count_ must fit in method_set's 32-bit bitmask");

// `count_` is the last enumerator (immediately after `patch`).
static_assert(static_cast<std::uint8_t>(httpserver::http_method::patch) + 1u
              == static_cast<std::uint8_t>(httpserver::http_method::count_),
              "count_ must remain the last enumerator (after patch)");

// Underlying type pinning.
static_assert(std::is_same_v<std::underlying_type_t<httpserver::http_method>,
                             std::uint8_t>,
              "http_method underlying type must be std::uint8_t");

// method_set storage pinning.
static_assert(std::is_standard_layout_v<httpserver::method_set>);
static_assert(std::is_trivially_copyable_v<httpserver::method_set>);
static_assert(sizeof(httpserver::method_set) == sizeof(std::uint32_t));
static_assert(std::is_same_v<decltype(httpserver::method_set::bits),
                             std::uint32_t>);

// Default-constructed method_set is empty.
static_assert(!httpserver::method_set{}.contains(httpserver::http_method::get));
static_assert(httpserver::method_set{}.bits == 0u);

// clear works.
static_assert(httpserver::method_set{}
                 .set(httpserver::http_method::get)
                 .clear(httpserver::http_method::get)
                 .bits == 0u);

// set_all sets exactly count_ bits.
static_assert(httpserver::method_set{}.set_all().bits
              == ((std::uint32_t{1} << static_cast<std::uint8_t>(
                       httpserver::http_method::count_)) - 1u));

// set_all() | clear_all() consistency.
static_assert(httpserver::method_set{}.set_all().clear_all().bits == 0u);

// Operator | on two enumerators.
static_assert(
    (httpserver::http_method::get | httpserver::http_method::post)
        .contains(httpserver::http_method::get));
static_assert(
    (httpserver::http_method::get | httpserver::http_method::post)
        .contains(httpserver::http_method::post));
static_assert(
    !(httpserver::http_method::get | httpserver::http_method::post)
        .contains(httpserver::http_method::put));

// Operator & on overlapping sets.
static_assert(
    ((httpserver::http_method::get | httpserver::http_method::post)
     & (httpserver::http_method::post | httpserver::http_method::put))
        .contains(httpserver::http_method::post));
static_assert(
    !((httpserver::http_method::get | httpserver::http_method::post)
      & (httpserver::http_method::post | httpserver::http_method::put))
        .contains(httpserver::http_method::get));

// Operator ^ (XOR) on enumerators yields union when disjoint, removes shared.
static_assert(
    (httpserver::http_method::get ^ httpserver::http_method::post).bits
    == ((httpserver::http_method::get | httpserver::http_method::post).bits));
static_assert(
    ((httpserver::http_method::get | httpserver::http_method::post)
     ^ (httpserver::http_method::post | httpserver::http_method::put)).bits
    == ((httpserver::http_method::get | httpserver::http_method::put).bits));

// Operator ~ on a method_set is bounded to the valid method window.
static_assert((~httpserver::method_set{}).bits
              == ((std::uint32_t{1} << static_cast<std::uint8_t>(
                       httpserver::http_method::count_)) - 1u));
static_assert((~httpserver::method_set{}.set_all()).bits == 0u);

// Operator ~ on an enumerator equals "all valid methods minus this one".
static_assert(!(~httpserver::http_method::get)
                  .contains(httpserver::http_method::get));
static_assert((~httpserver::http_method::get)
                  .contains(httpserver::http_method::post));

// Compound assignment usable in constant context.
static_assert([] {
    httpserver::method_set s{};
    s |= httpserver::http_method::get;
    s |= httpserver::http_method::post;
    s &= (httpserver::http_method::post | httpserver::http_method::put);
    return s.contains(httpserver::http_method::post)
           && !s.contains(httpserver::http_method::get);
}());

// to_string returns the wire-protocol uppercase tokens.
static_assert(httpserver::to_string(httpserver::http_method::get)
              == std::string_view{"GET"});
static_assert(httpserver::to_string(httpserver::http_method::head)
              == std::string_view{"HEAD"});
static_assert(httpserver::to_string(httpserver::http_method::post)
              == std::string_view{"POST"});
static_assert(httpserver::to_string(httpserver::http_method::put)
              == std::string_view{"PUT"});
static_assert(httpserver::to_string(httpserver::http_method::del)
              == std::string_view{"DELETE"});
static_assert(httpserver::to_string(httpserver::http_method::connect)
              == std::string_view{"CONNECT"});
static_assert(httpserver::to_string(httpserver::http_method::options)
              == std::string_view{"OPTIONS"});
static_assert(httpserver::to_string(httpserver::http_method::trace)
              == std::string_view{"TRACE"});
static_assert(httpserver::to_string(httpserver::http_method::patch)
              == std::string_view{"PATCH"});

// Out-of-range to_string returns an empty view (does not crash).
static_assert(httpserver::to_string(static_cast<httpserver::http_method>(99))
              == std::string_view{});

LT_BEGIN_SUITE(http_method_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_method_suite)

// 1. Runtime mirror of AC #1.
LT_BEGIN_AUTO_TEST(http_method_suite, set_then_contains_runtime)
    httpserver::method_set s{};
    s.set(httpserver::http_method::get);
    LT_CHECK(s.contains(httpserver::http_method::get));
LT_END_AUTO_TEST(set_then_contains_runtime)

// 2. set then clear returns bits == 0.
LT_BEGIN_AUTO_TEST(http_method_suite, set_clear_roundtrip)
    httpserver::method_set s{};
    s.set(httpserver::http_method::post);
    LT_CHECK(s.contains(httpserver::http_method::post));
    s.clear(httpserver::http_method::post);
    LT_CHECK(!s.contains(httpserver::http_method::post));
    LT_CHECK_EQ(s.bits, 0u);
LT_END_AUTO_TEST(set_clear_roundtrip)

// 3. set_all then contains every declared method.
LT_BEGIN_AUTO_TEST(http_method_suite, set_all_then_contains_every_method)
    httpserver::method_set s{};
    s.set_all();
    const auto count = static_cast<std::uint8_t>(httpserver::http_method::count_);
    for (std::uint8_t i = 0; i < count; ++i) {
        LT_CHECK(s.contains(static_cast<httpserver::http_method>(i)));
    }
LT_END_AUTO_TEST(set_all_then_contains_every_method)

// 4. clear_all makes empty.
LT_BEGIN_AUTO_TEST(http_method_suite, clear_all_makes_empty)
    httpserver::method_set s{};
    s.set_all();
    s.clear_all();
    const auto count = static_cast<std::uint8_t>(httpserver::http_method::count_);
    for (std::uint8_t i = 0; i < count; ++i) {
        LT_CHECK(!s.contains(static_cast<httpserver::http_method>(i)));
    }
    LT_CHECK_EQ(s.bits, 0u);
LT_END_AUTO_TEST(clear_all_makes_empty)

// 5. Bitwise OR on two enumerators yields a set with both.
LT_BEGIN_AUTO_TEST(http_method_suite, bitwise_or_two_enumerators_yields_set_with_both)
    auto s = httpserver::http_method::get | httpserver::http_method::post;
    LT_CHECK(s.contains(httpserver::http_method::get));
    LT_CHECK(s.contains(httpserver::http_method::post));
    LT_CHECK(!s.contains(httpserver::http_method::put));
LT_END_AUTO_TEST(bitwise_or_two_enumerators_yields_set_with_both)

// 6. Bitwise AND intersection.
LT_BEGIN_AUTO_TEST(http_method_suite, bitwise_and_intersection)
    auto a = httpserver::http_method::get | httpserver::http_method::post;
    auto b = httpserver::http_method::post | httpserver::http_method::put;
    auto inter = a & b;
    LT_CHECK(inter.contains(httpserver::http_method::post));
    LT_CHECK(!inter.contains(httpserver::http_method::get));
    LT_CHECK(!inter.contains(httpserver::http_method::put));
LT_END_AUTO_TEST(bitwise_and_intersection)

// 7. Bitwise XOR symmetric difference.
LT_BEGIN_AUTO_TEST(http_method_suite, bitwise_xor_symmetric_difference)
    auto a = httpserver::http_method::get | httpserver::http_method::post;
    auto b = httpserver::http_method::post | httpserver::http_method::put;
    auto symdiff = a ^ b;
    LT_CHECK(symdiff.contains(httpserver::http_method::get));
    LT_CHECK(symdiff.contains(httpserver::http_method::put));
    LT_CHECK(!symdiff.contains(httpserver::http_method::post));
LT_END_AUTO_TEST(bitwise_xor_symmetric_difference)

// 8. Complement of a singleton contains every other declared method.
LT_BEGIN_AUTO_TEST(http_method_suite, complement_of_singleton_contains_every_other_method)
    auto comp = ~httpserver::http_method::get;
    LT_CHECK(!comp.contains(httpserver::http_method::get));
    const auto count = static_cast<std::uint8_t>(httpserver::http_method::count_);
    for (std::uint8_t i = 0; i < count; ++i) {
        if (i == static_cast<std::uint8_t>(httpserver::http_method::get)) {
            continue;
        }
        LT_CHECK(comp.contains(static_cast<httpserver::http_method>(i)));
    }
LT_END_AUTO_TEST(complement_of_singleton_contains_every_other_method)

// 9. Complement of a method_set is bounded to the count_ window.
LT_BEGIN_AUTO_TEST(http_method_suite, complement_of_set_is_bounded_to_count_window)
    httpserver::method_set empty{};
    auto full = ~empty;
    LT_CHECK_EQ(full.bits, httpserver::method_set{}.set_all().bits);
    // Bits beyond count_ must be zero.
    const auto count = static_cast<std::uint8_t>(httpserver::http_method::count_);
    const std::uint32_t valid_mask = (std::uint32_t{1} << count) - 1u;
    LT_CHECK_EQ(full.bits & ~valid_mask, 0u);
LT_END_AUTO_TEST(complement_of_set_is_bounded_to_count_window)

// 10. Compound assignment with enumerator and method_set.
LT_BEGIN_AUTO_TEST(http_method_suite, compound_assign_or_equals_with_enumerator)
    httpserver::method_set s{};
    s |= httpserver::http_method::get;
    s |= httpserver::http_method::post;
    LT_CHECK(s.contains(httpserver::http_method::get));
    LT_CHECK(s.contains(httpserver::http_method::post));

    s &= (httpserver::http_method::post | httpserver::http_method::put);
    LT_CHECK(!s.contains(httpserver::http_method::get));
    LT_CHECK(s.contains(httpserver::http_method::post));
    LT_CHECK(!s.contains(httpserver::http_method::put));

    s ^= httpserver::http_method::post;
    LT_CHECK(!s.contains(httpserver::http_method::post));
    LT_CHECK_EQ(s.bits, 0u);
LT_END_AUTO_TEST(compound_assign_or_equals_with_enumerator)

// 11. to_string returns the uppercase wire-protocol tokens.
LT_BEGIN_AUTO_TEST(http_method_suite, to_string_returns_uppercase_wire_tokens)
    LT_CHECK(httpserver::to_string(httpserver::http_method::get)     == std::string_view{"GET"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::head)    == std::string_view{"HEAD"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::post)    == std::string_view{"POST"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::put)     == std::string_view{"PUT"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::del)     == std::string_view{"DELETE"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::connect) == std::string_view{"CONNECT"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::options) == std::string_view{"OPTIONS"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::trace)   == std::string_view{"TRACE"});
    LT_CHECK(httpserver::to_string(httpserver::http_method::patch)   == std::string_view{"PATCH"});
LT_END_AUTO_TEST(to_string_returns_uppercase_wire_tokens)

// 12. to_string of an unknown enum value returns an empty view.
LT_BEGIN_AUTO_TEST(http_method_suite, to_string_unknown_returns_empty_view)
    auto sv = httpserver::to_string(static_cast<httpserver::http_method>(99));
    LT_CHECK(sv.empty());
LT_END_AUTO_TEST(to_string_unknown_returns_empty_view)

// 13. to_string matches the libmicrohttpd wire tokens. This is the
// contract that lets routing match libmicrohttpd's method strings against
// to_string(http_method). MHD method-string macros expand to literal C
// strings ("GET", "DELETE", ...), so direct comparison is well-defined.
LT_BEGIN_AUTO_TEST(http_method_suite, to_string_round_trip_via_strcmp_with_mhd)
    LT_CHECK(httpserver::to_string(httpserver::http_method::get)
             == std::string_view{MHD_HTTP_METHOD_GET});
    LT_CHECK(httpserver::to_string(httpserver::http_method::head)
             == std::string_view{MHD_HTTP_METHOD_HEAD});
    LT_CHECK(httpserver::to_string(httpserver::http_method::post)
             == std::string_view{MHD_HTTP_METHOD_POST});
    LT_CHECK(httpserver::to_string(httpserver::http_method::put)
             == std::string_view{MHD_HTTP_METHOD_PUT});
    LT_CHECK(httpserver::to_string(httpserver::http_method::del)
             == std::string_view{MHD_HTTP_METHOD_DELETE});
    LT_CHECK(httpserver::to_string(httpserver::http_method::connect)
             == std::string_view{MHD_HTTP_METHOD_CONNECT});
    LT_CHECK(httpserver::to_string(httpserver::http_method::options)
             == std::string_view{MHD_HTTP_METHOD_OPTIONS});
    LT_CHECK(httpserver::to_string(httpserver::http_method::trace)
             == std::string_view{MHD_HTTP_METHOD_TRACE});
    LT_CHECK(httpserver::to_string(httpserver::http_method::patch)
             == std::string_view{MHD_HTTP_METHOD_PATCH});
LT_END_AUTO_TEST(to_string_round_trip_via_strcmp_with_mhd)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
