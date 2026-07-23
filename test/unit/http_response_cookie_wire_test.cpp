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

// http_response::with_cookie(cookie) integration.
//
// Pins:
//   * structured with_cookie(cookie) appends to get_cookies_parsed()
//     in insertion order and survives moves;
//   * legacy with_cookie(string, string) still works (deprecated) and
//     mirrors into BOTH the legacy map AND the structured vector;
//   * the wire-render path -- which now reads structured_cookies_ via
//     get_cookies_parsed() -- produces exactly one Set-Cookie header
//     per structured entry, via cookie::to_set_cookie_header();
//   * mixing both paths does NOT double-emit on the wire.
//
// Wire emission is asserted by walking the structured cookie vector and
// invoking the renderer the dispatch path uses
// (cookie::to_set_cookie_header). This is the only structured-cookie
// contract the dispatch path is allowed to depend on -- the actual
// MHD_add_response_header invocation in decorate_mhd_response is a
// trivial loop and is covered by the integration test
// `test/integ/basic.cpp::request_with_cookie` end-to-end via curl.

#include <microhttpd.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// Deprecated-path tests in this TU intentionally exercise the legacy
// string-blob overload.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

using httpserver::cookie;
using httpserver::http_response;
using httpserver::same_site_mode;

LT_BEGIN_SUITE(http_response_cookie_wire_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http_response_cookie_wire_suite)

// ---------------- with_cookie(cookie) integration ----------------

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   with_cookie_struct_appends_to_get_cookies_parsed)
    http_response r = http_response::string("body");
    r.with_cookie(cookie{}.with_name("sid").with_value("abc")
                    .with_secure(true));
    const auto& v = r.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("sid"));
    LT_CHECK_EQ(v[0].value(), std::string("abc"));
    LT_CHECK_EQ(v[0].is_secure(), true);
LT_END_AUTO_TEST(with_cookie_struct_appends_to_get_cookies_parsed)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   with_cookie_struct_lvalue_returns_lvalue_ref)
    using R = http_response;
    static_assert(std::is_same_v<
        decltype(std::declval<R&>().with_cookie(std::declval<cookie>())),
        R&>, "with_cookie(cookie) & must return http_response&");
LT_END_AUTO_TEST(with_cookie_struct_lvalue_returns_lvalue_ref)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   with_cookie_struct_rvalue_returns_rvalue_ref)
    using R = http_response;
    static_assert(std::is_same_v<
        decltype(std::declval<R&&>().with_cookie(std::declval<cookie>())),
        R&&>, "with_cookie(cookie) && must return http_response&&");
LT_END_AUTO_TEST(with_cookie_struct_rvalue_returns_rvalue_ref)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   get_cookies_parsed_return_type_is_const_vector_ref)
    using R = http_response;
    static_assert(std::is_same_v<
        decltype(std::declval<const R&>().get_cookies_parsed()),
        const std::vector<cookie>&>,
        "get_cookies_parsed() must return const std::vector<cookie>&");
    static_assert(noexcept(std::declval<const R&>().get_cookies_parsed()),
                  "get_cookies_parsed() must be noexcept");
LT_END_AUTO_TEST(get_cookies_parsed_return_type_is_const_vector_ref)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   multiple_with_cookie_calls_produce_separate_entries)
    http_response r = http_response::string("body");
    r.with_cookie(cookie{}.with_name("a").with_value("1"));
    r.with_cookie(cookie{}.with_name("b").with_value("2"));
    r.with_cookie(cookie{}.with_name("c").with_value("3"));
    const auto& v = r.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(3));
    LT_CHECK_EQ(v[0].name(), std::string("a"));
    LT_CHECK_EQ(v[1].name(), std::string("b"));
    LT_CHECK_EQ(v[2].name(), std::string("c"));
LT_END_AUTO_TEST(multiple_with_cookie_calls_produce_separate_entries)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   get_cookies_parsed_stable_across_other_mutations)
    http_response r = http_response::string("body");
    r.with_cookie(cookie{}.with_name("sid").with_value("x"));
    const std::vector<cookie>* p1 = &r.get_cookies_parsed();
    // Unrelated mutation: add a header.
    r.with_header("X-Custom", "yes");
    const std::vector<cookie>* p2 = &r.get_cookies_parsed();
    LT_CHECK_EQ(p1, p2);
LT_END_AUTO_TEST(get_cookies_parsed_stable_across_other_mutations)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   structured_cookies_survive_move_ctor)
    http_response src = http_response::string("body");
    src.with_cookie(cookie{}.with_name("sid").with_value("abc")
                    .with_same_site(same_site_mode::strict));
    http_response dst(std::move(src));
    const auto& v = dst.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("sid"));
    LT_CHECK_EQ(v[0].same_site() == same_site_mode::strict, true);
LT_END_AUTO_TEST(structured_cookies_survive_move_ctor)

// ---------------- legacy / structured interop ----------------

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   legacy_string_pair_mirrors_into_get_cookies_parsed)
    http_response r = http_response::string("body");
    r.with_cookie("sid", "abc");
    const auto& v = r.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("sid"));
    LT_CHECK_EQ(v[0].value(), std::string("abc"));
    // No attribute info on the legacy path.
    LT_CHECK_EQ(v[0].is_secure(), false);
    LT_CHECK_EQ(v[0].is_http_only(), false);
LT_END_AUTO_TEST(legacy_string_pair_mirrors_into_get_cookies_parsed)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   legacy_string_pair_still_populates_legacy_map)
    // Backward-compat: legacy `get_cookie(name)` must keep returning
    // the value after a legacy `with_cookie(name, value)` call.
    http_response r = http_response::string("body");
    r.with_cookie("sid", "abc");
    LT_CHECK_EQ(r.get_cookie("sid"), std::string_view("abc"));
LT_END_AUTO_TEST(legacy_string_pair_still_populates_legacy_map)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   mixed_paths_dont_double_emit_on_wire)
    // The dispatch path renders ONE Set-Cookie per get_cookies_parsed
    // entry. The legacy `cookies_` mirror is observation-only -- never
    // a render source. Mixing both setters must produce exactly N+M
    // wire entries for N legacy + M structured calls.
    http_response r = http_response::string("body");
    r.with_cookie("legacy1", "v1");
    r.with_cookie(cookie{}.with_name("struct1").with_value("v2"));
    r.with_cookie("legacy2", "v3");
    const auto& v = r.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(3));
    LT_CHECK_EQ(v[0].name(), std::string("legacy1"));
    LT_CHECK_EQ(v[1].name(), std::string("struct1"));
    LT_CHECK_EQ(v[2].name(), std::string("legacy2"));
LT_END_AUTO_TEST(mixed_paths_dont_double_emit_on_wire)

// ---------------- wire-format pin via to_set_cookie_header -------------

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   each_structured_cookie_renders_to_one_set_cookie_value)
    http_response r = http_response::string("body");
    r.with_cookie(cookie{}.with_name("a").with_value("1")
                          .with_path("/"));
    r.with_cookie(cookie{}.with_name("b").with_value("2")
                          .with_secure(true)
                          .with_same_site(same_site_mode::strict));
    r.with_cookie(cookie{}.with_name("c").with_value("3")
                          .with_http_only(true));
    const auto& v = r.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(3));
    LT_CHECK_EQ(v[0].to_set_cookie_header(),
                std::string("a=1; Path=/"));
    LT_CHECK_EQ(v[1].to_set_cookie_header(),
                std::string("b=2; Secure; SameSite=Strict"));
    LT_CHECK_EQ(v[2].to_set_cookie_header(),
                std::string("c=3; HttpOnly"));
LT_END_AUTO_TEST(each_structured_cookie_renders_to_one_set_cookie_value)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   wire_renderer_emits_full_attribute_set)
    // Headline contract: a cookie with name/value/secure/strict renders to
    // an RFC 6265 §4.1 well-formed Set-Cookie header value.
    http_response r = http_response::string("body");
    r.with_cookie(cookie{}.with_name("sid").with_value("abc")
                          .with_secure(true)
                          .with_same_site(same_site_mode::strict));
    const auto& v = r.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].to_set_cookie_header(),
                std::string("sid=abc; Secure; SameSite=Strict"));
LT_END_AUTO_TEST(wire_renderer_emits_full_attribute_set)

// ---------------- partial-mutation guard on setter throw -----------

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   legacy_with_cookie_bad_key_leaves_maps_clean)
    // Regression: if with_name throws (key contains ';'), the legacy
    // cookies_ map must NOT be mutated. Before the fix, insert_or_assign
    // ran before with_name, so the map got a dirty entry on failure.
    http_response r = http_response::string("body");
    bool threw = false;
    try {
        r.with_cookie("bad;key", "value");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
    // The bad key must NOT appear in the legacy map.
    LT_CHECK_EQ(r.get_cookie("bad;key"), std::string_view(""));
    // The structured vector must also be empty.
    LT_CHECK_EQ(r.get_cookies_parsed().empty(), true);
LT_END_AUTO_TEST(legacy_with_cookie_bad_key_leaves_maps_clean)

LT_BEGIN_AUTO_TEST(http_response_cookie_wire_suite,
                   legacy_with_cookie_bad_value_leaves_maps_clean)
    // Symmetric to legacy_with_cookie_bad_key_leaves_maps_clean above:
    // with_value throws before any map mutation when the value contains
    // ';', so the same clean-on-failure guarantee must hold for a bad
    // value as it does for a bad key.
    http_response r = http_response::string("body");
    bool threw = false;
    try {
        r.with_cookie("good_key", "bad;value");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
    // The good key must NOT appear in the legacy map either.
    LT_CHECK_EQ(r.get_cookie("good_key"), std::string_view(""));
    // The structured vector must also be empty.
    LT_CHECK_EQ(r.get_cookies_parsed().empty(), true);
LT_END_AUTO_TEST(legacy_with_cookie_bad_value_leaves_maps_clean)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
