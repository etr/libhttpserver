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

#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "./littletest.hpp"
#include "./httpserver.hpp"

// TASK-064: this TU intentionally exercises the deprecated string-blob
// cookie surface (`with_cookie(string, string)`, `get_cookie(...)`,
// `get_cookies()`). Suppress the [[deprecated]] diagnostic for the
// whole file -- the legacy path still works through a thin
// forwarder, and the tests must keep passing during the v2.0
// transitional release. The structured-overload behaviour is pinned
// by cookie_render_test, cookie_header_sentinel_test, and the
// http_response_cookie_wire_test added in TASK-064.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

using std::string;
using httpserver::http_response;

LT_BEGIN_SUITE(http_response_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_response_suite)

LT_BEGIN_AUTO_TEST(http_response_suite, default_response_code)
    http_response resp;
    LT_CHECK_EQ(resp.get_status(), -1);
LT_END_AUTO_TEST(default_response_code)

LT_BEGIN_AUTO_TEST(http_response_suite, factory_status_404)
    http_response resp = http_response::string("Not Found").with_status(404);
    LT_CHECK_EQ(resp.get_status(), 404);
LT_END_AUTO_TEST(factory_status_404)

LT_BEGIN_AUTO_TEST(http_response_suite, get_header_returns_value_set_via_with_header)
    http_response resp = http_response::string("body");
    resp.with_header("X-Custom-Header", "HeaderValue");
    LT_CHECK_EQ(resp.get_header("X-Custom-Header"), "HeaderValue");
LT_END_AUTO_TEST(get_header_returns_value_set_via_with_header)

LT_BEGIN_AUTO_TEST(http_response_suite, get_footer_returns_value_set_via_with_footer)
    http_response resp = http_response::string("body");
    resp.with_footer("X-Footer", "FooterValue");
    LT_CHECK_EQ(resp.get_footer("X-Footer"), "FooterValue");
LT_END_AUTO_TEST(get_footer_returns_value_set_via_with_footer)

LT_BEGIN_AUTO_TEST(http_response_suite, get_cookie_returns_value_set_via_with_cookie)
    http_response resp = http_response::string("body");
    resp.with_cookie("SessionId", "abc123");
    LT_CHECK_EQ(resp.get_cookie("SessionId"), "abc123");
LT_END_AUTO_TEST(get_cookie_returns_value_set_via_with_cookie)

LT_BEGIN_AUTO_TEST(http_response_suite, get_headers)
    http_response resp = http_response::string("body");
    resp.with_header("Header1", "Value1");
    resp.with_header("Header2", "Value2");
    const auto& headers = resp.get_headers();
    LT_CHECK_EQ(headers.at("Header1"), "Value1");
    LT_CHECK_EQ(headers.at("Header2"), "Value2");
LT_END_AUTO_TEST(get_headers)

LT_BEGIN_AUTO_TEST(http_response_suite, get_footers)
    http_response resp = http_response::string("body");
    resp.with_footer("Footer1", "Value1");
    resp.with_footer("Footer2", "Value2");
    const auto& footers = resp.get_footers();
    LT_CHECK_EQ(footers.at("Footer1"), "Value1");
    LT_CHECK_EQ(footers.at("Footer2"), "Value2");
LT_END_AUTO_TEST(get_footers)

LT_BEGIN_AUTO_TEST(http_response_suite, get_cookies)
    http_response resp = http_response::string("body");
    resp.with_cookie("Cookie1", "Value1");
    resp.with_cookie("Cookie2", "Value2");
    const auto& cookies = resp.get_cookies();
    LT_CHECK_EQ(cookies.at("Cookie1"), "Value1");
    LT_CHECK_EQ(cookies.at("Cookie2"), "Value2");
LT_END_AUTO_TEST(get_cookies)

LT_BEGIN_AUTO_TEST(http_response_suite, shoutcast_response)
    http_response resp = http_response::string("OK", "audio/mpeg");
    int original_code = resp.get_status();
    resp.shoutCAST();
    // shoutCAST sets the MHD_ICY_FLAG (1 << 31) on response_code
    // Verify the flag bit is set (use unsigned comparison)
    LT_CHECK_EQ(static_cast<unsigned int>(resp.get_status()) & 0x80000000u, 0x80000000u);
    // Also verify the original code bits are preserved
    LT_CHECK_EQ(resp.get_status() & 0x7FFFFFFF, original_code);
LT_END_AUTO_TEST(shoutcast_response)

LT_BEGIN_AUTO_TEST(http_response_suite, factory_string_default_status)
    http_response resp = http_response::string("Hello World");
    // Should use default response code (200) and content type (text/plain)
    LT_CHECK_EQ(resp.get_status(), 200);
LT_END_AUTO_TEST(factory_string_default_status)

LT_BEGIN_AUTO_TEST(http_response_suite, ostream_operator_empty)
    // Test ostream operator with default response (no headers/footers/cookies)
    http_response resp;  // Default constructor - no content type header added
    std::ostringstream oss;
    oss << resp;
    string output = oss.str();
    // With empty headers/footers/cookies, only the response code line is output
    LT_CHECK_EQ(output.find("Response [response_code:-1]") != string::npos, true);
    // Empty maps don't produce any output in dump_header_map
    LT_CHECK_EQ(output.find("Headers [") == string::npos, true);
    LT_CHECK_EQ(output.find("Footers [") == string::npos, true);
    LT_CHECK_EQ(output.find("Cookies [") == string::npos, true);
LT_END_AUTO_TEST(ostream_operator_empty)

LT_BEGIN_AUTO_TEST(http_response_suite, ostream_operator_full)
    // Test ostream operator with headers, footers, and cookies
    http_response resp = http_response::string("body", "application/json")
                             .with_status(201);
    resp.with_header("X-Header1", "Value1");
    resp.with_header("X-Header2", "Value2");
    resp.with_footer("X-Footer", "FooterVal");
    resp.with_cookie("SessionId", "abc123");
    resp.with_cookie("UserId", "user42");

    std::ostringstream oss;
    oss << resp;
    string output = oss.str();

    LT_CHECK_EQ(output.find("Response [response_code:201]") != string::npos, true);
    LT_CHECK_EQ(output.find("X-Header1") != string::npos, true);
    LT_CHECK_EQ(output.find("X-Header2") != string::npos, true);
    LT_CHECK_EQ(output.find("X-Footer") != string::npos, true);
    LT_CHECK_EQ(output.find("SessionId") != string::npos, true);
    LT_CHECK_EQ(output.find("UserId") != string::npos, true);
LT_END_AUTO_TEST(ostream_operator_full)

// Test multiple headers
LT_BEGIN_AUTO_TEST(http_response_suite, multiple_headers)
    http_response resp = http_response::string("body");
    resp.with_header("H1", "V1");
    resp.with_header("H2", "V2");
    resp.with_header("H3", "V3");
    LT_CHECK_EQ(resp.get_header("H1"), "V1");
    LT_CHECK_EQ(resp.get_header("H2"), "V2");
    LT_CHECK_EQ(resp.get_header("H3"), "V3");
LT_END_AUTO_TEST(multiple_headers)

// Test multiple footers
LT_BEGIN_AUTO_TEST(http_response_suite, multiple_footers)
    http_response resp = http_response::string("body");
    resp.with_footer("F1", "V1");
    resp.with_footer("F2", "V2");
    LT_CHECK_EQ(resp.get_footer("F1"), "V1");
    LT_CHECK_EQ(resp.get_footer("F2"), "V2");
LT_END_AUTO_TEST(multiple_footers)

// Test multiple cookies
LT_BEGIN_AUTO_TEST(http_response_suite, multiple_cookies)
    http_response resp = http_response::string("body");
    resp.with_cookie("C1", "V1");
    resp.with_cookie("C2", "V2");
    LT_CHECK_EQ(resp.get_cookie("C1"), "V1");
    LT_CHECK_EQ(resp.get_cookie("C2"), "V2");
LT_END_AUTO_TEST(multiple_cookies)

// Test overwriting header
LT_BEGIN_AUTO_TEST(http_response_suite, overwrite_header)
    http_response resp = http_response::string("body");
    resp.with_header("Key", "Value1");
    LT_CHECK_EQ(resp.get_header("Key"), "Value1");
    resp.with_header("Key", "Value2");
    LT_CHECK_EQ(resp.get_header("Key"), "Value2");
LT_END_AUTO_TEST(overwrite_header)

// Test overwriting cookie
LT_BEGIN_AUTO_TEST(http_response_suite, overwrite_cookie)
    http_response resp = http_response::string("body");
    resp.with_cookie("Cookie", "OldValue");
    LT_CHECK_EQ(resp.get_cookie("Cookie"), "OldValue");
    resp.with_cookie("Cookie", "NewValue");
    LT_CHECK_EQ(resp.get_cookie("Cookie"), "NewValue");
LT_END_AUTO_TEST(overwrite_cookie)

// Test empty headers map (using default constructor to get truly empty headers)
LT_BEGIN_AUTO_TEST(http_response_suite, empty_headers_map)
    http_response resp;  // Default constructor - no content type header added
    const auto& headers = resp.get_headers();
    LT_CHECK_EQ(headers.empty(), true);
LT_END_AUTO_TEST(empty_headers_map)

// Test empty footers map
LT_BEGIN_AUTO_TEST(http_response_suite, empty_footers_map)
    http_response resp = http_response::string("body");
    const auto& footers = resp.get_footers();
    LT_CHECK_EQ(footers.empty(), true);
LT_END_AUTO_TEST(empty_footers_map)

// Test empty cookies map
LT_BEGIN_AUTO_TEST(http_response_suite, empty_cookies_map)
    http_response resp = http_response::string("body");
    const auto& cookies = resp.get_cookies();
    LT_CHECK_EQ(cookies.empty(), true);
LT_END_AUTO_TEST(empty_cookies_map)

// Test ostream with only headers
LT_BEGIN_AUTO_TEST(http_response_suite, ostream_operator_headers_only)
    http_response resp = http_response::string("body");
    resp.with_header("X-Custom", "Value");
    std::ostringstream oss;
    oss << resp;
    string output = oss.str();
    LT_CHECK_EQ(output.find("X-Custom") != string::npos, true);
    LT_CHECK_EQ(output.find("200") != string::npos, true);
LT_END_AUTO_TEST(ostream_operator_headers_only)

// Test ostream with only footers
LT_BEGIN_AUTO_TEST(http_response_suite, ostream_operator_footers_only)
    http_response resp = http_response::string("body");
    resp.with_footer("X-Footer", "FootVal");
    std::ostringstream oss;
    oss << resp;
    string output = oss.str();
    LT_CHECK_EQ(output.find("X-Footer") != string::npos, true);
LT_END_AUTO_TEST(ostream_operator_footers_only)

// Test ostream with only cookies
LT_BEGIN_AUTO_TEST(http_response_suite, ostream_operator_cookies_only)
    http_response resp = http_response::string("body");
    resp.with_cookie("Session", "abc123");
    std::ostringstream oss;
    oss << resp;
    string output = oss.str();
    LT_CHECK_EQ(output.find("Session") != string::npos, true);
LT_END_AUTO_TEST(ostream_operator_cookies_only)

// Test http_response::string factory with all parameters
LT_BEGIN_AUTO_TEST(http_response_suite, factory_string_full_params)
    http_response resp = http_response::string("Body content", "application/json")
                             .with_status(201);
    LT_CHECK_EQ(resp.get_status(), 201);
    LT_CHECK_EQ(resp.get_header("Content-Type"),
                std::string_view("application/json"));
LT_END_AUTO_TEST(factory_string_full_params)

// Test http_response with content_type parameter
LT_BEGIN_AUTO_TEST(http_response_suite, factory_string_with_content_type)
    http_response resp = http_response::string("body", "application/json");
    LT_CHECK_EQ(resp.get_status(), 200);
    LT_CHECK_EQ(resp.get_header("Content-Type"),
                std::string_view("application/json"));
LT_END_AUTO_TEST(factory_string_with_content_type)

// Test special characters in header values
LT_BEGIN_AUTO_TEST(http_response_suite, header_special_characters)
    http_response resp = http_response::string("body");
    resp.with_header("Content-Disposition", "attachment; filename=\"file.txt\"");
    LT_CHECK_EQ(resp.get_header("Content-Disposition"), "attachment; filename=\"file.txt\"");
LT_END_AUTO_TEST(header_special_characters)

// Test special characters in cookie values
LT_BEGIN_AUTO_TEST(http_response_suite, cookie_special_characters)
    http_response resp = http_response::string("body");
    resp.with_cookie("Data", "value=with=equals");
    LT_CHECK_EQ(resp.get_cookie("Data"), "value=with=equals");
LT_END_AUTO_TEST(cookie_special_characters)

// =====================================================================
// TASK-011: const-correct accessors. The single-key accessors must be
// callable on a const http_response&, return std::string_view, and must
// NOT insert on miss. The map-returning accessors and the trivial
// scalar accessors (get_status, kind) must be noexcept.
// =====================================================================

// AC #1: `void f(const http_response& r) { auto v = r.get_header("X-Foo"); }`
// compiles. Also pins down the return type.
LT_BEGIN_AUTO_TEST(http_response_suite, get_header_const_callable)
    http_response resp = http_response::string("body");
    resp.with_header("X-Foo", "bar");
    const http_response& cref = resp;
    auto v = cref.get_header("X-Foo");
    static_assert(std::is_same_v<decltype(v), std::string_view>,
                  "get_header on const& must return std::string_view");
    LT_CHECK_EQ(v, std::string_view("bar"));
LT_END_AUTO_TEST(get_header_const_callable)

// AC #2: get_header on a missing key does NOT insert — headers map size
// is unchanged after the lookup.
LT_BEGIN_AUTO_TEST(http_response_suite, get_header_no_insert_on_miss)
    http_response resp = http_response::string("body");
    resp.with_header("X-Present", "value");
    const std::size_t before = resp.get_headers().size();
    const http_response& cref = resp;
    auto v = cref.get_header("X-Missing");
    LT_CHECK_EQ(v.empty(), true);
    LT_CHECK_EQ(resp.get_headers().size(), before);
LT_END_AUTO_TEST(get_header_no_insert_on_miss)

LT_BEGIN_AUTO_TEST(http_response_suite, get_footer_no_insert_on_miss)
    http_response resp = http_response::string("body");
    resp.with_footer("F-Present", "value");
    const std::size_t before = resp.get_footers().size();
    const http_response& cref = resp;
    auto v = cref.get_footer("F-Missing");
    LT_CHECK_EQ(v.empty(), true);
    LT_CHECK_EQ(resp.get_footers().size(), before);
LT_END_AUTO_TEST(get_footer_no_insert_on_miss)

LT_BEGIN_AUTO_TEST(http_response_suite, get_cookie_no_insert_on_miss)
    http_response resp = http_response::string("body");
    resp.with_cookie("C-Present", "value");
    const std::size_t before = resp.get_cookies().size();
    const http_response& cref = resp;
    auto v = cref.get_cookie("C-Missing");
    LT_CHECK_EQ(v.empty(), true);
    LT_CHECK_EQ(resp.get_cookies().size(), before);
LT_END_AUTO_TEST(get_cookie_no_insert_on_miss)

// AC #3: read back a header set via with_header from a `const&` reference.
LT_BEGIN_AUTO_TEST(http_response_suite, get_header_const_reference_after_with_header)
    http_response resp = http_response::string("body");
    resp.with_header("X-Set-Via-With", "the-value");
    const http_response& cref = resp;
    LT_CHECK_EQ(cref.get_header("X-Set-Via-With"), std::string_view("the-value"));
LT_END_AUTO_TEST(get_header_const_reference_after_with_header)

LT_BEGIN_AUTO_TEST(http_response_suite, get_status_const_callable)
    http_response resp = http_response::string("body");
    static_assert(noexcept(std::declval<const http_response&>().get_status()),
                  "get_status() must be noexcept");
    static_assert(std::is_same_v<decltype(std::declval<const http_response&>()
                                              .get_status()),
                                 int>,
                  "get_status() must return int");
    const http_response& cref = resp;
    LT_CHECK_EQ(cref.get_status(), 200);
LT_END_AUTO_TEST(get_status_const_callable)

LT_BEGIN_AUTO_TEST(http_response_suite, kind_const_callable)
    http_response resp = http_response::string("body");
    static_assert(noexcept(std::declval<const http_response&>().kind()),
                  "kind() must be noexcept");
    const http_response& cref = resp;
    LT_CHECK_EQ(cref.kind() == httpserver::body_kind::string, true);
LT_END_AUTO_TEST(kind_const_callable)

LT_BEGIN_AUTO_TEST(http_response_suite, map_accessors_are_noexcept)
    static_assert(noexcept(std::declval<const http_response&>().get_headers()),
                  "get_headers() must be noexcept");
    static_assert(noexcept(std::declval<const http_response&>().get_footers()),
                  "get_footers() must be noexcept");
    static_assert(noexcept(std::declval<const http_response&>().get_cookies()),
                  "get_cookies() must be noexcept");
    // Runtime smoke check so the suite has at least one runtime assertion.
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(map_accessors_are_noexcept)

LT_BEGIN_AUTO_TEST(http_response_suite, get_headers_returns_stable_const_ref)
    http_response resp = http_response::string("body");
    const http_response& cref = resp;
    // Returns by const reference: the same address comes back twice.
    const auto& m1 = cref.get_headers();
    const auto& m2 = cref.get_headers();
    LT_CHECK_EQ(&m1 == &m2, true);
LT_END_AUTO_TEST(get_headers_returns_stable_const_ref)

LT_BEGIN_AUTO_TEST(http_response_suite, single_key_accessors_take_string_view)
    // Direct invocability check via member function pointer types.
    using GetHeaderFn = std::string_view (http_response::*)(std::string_view) const;
    using GetFooterFn = std::string_view (http_response::*)(std::string_view) const;
    using GetCookieFn = std::string_view (http_response::*)(std::string_view) const;
    GetHeaderFn h = &http_response::get_header;
    GetFooterFn f = &http_response::get_footer;
    GetCookieFn c = &http_response::get_cookie;
    (void)h;
    (void)f;
    (void)c;
    // Also a smoke runtime check that a string_view literal works directly.
    http_response resp = http_response::string("body");
    resp.with_header("X-K", "v");
    const http_response& cref = resp;
    std::string_view key("X-K");
    LT_CHECK_EQ(cref.get_header(key), std::string_view("v"));
LT_END_AUTO_TEST(single_key_accessors_take_string_view)

LT_BEGIN_AUTO_TEST(http_response_suite, header_lookup_is_case_insensitive)
    http_response resp = http_response::string("body");
    resp.with_header("X-Foo", "bar");
    const http_response& cref = resp;
    LT_CHECK_EQ(cref.get_header("x-foo"), std::string_view("bar"));
    LT_CHECK_EQ(cref.get_header("X-FOO"), std::string_view("bar"));
LT_END_AUTO_TEST(header_lookup_is_case_insensitive)

LT_BEGIN_AUTO_TEST(http_response_suite, footer_lookup_is_case_insensitive)
    http_response resp = http_response::string("body");
    resp.with_footer("X-Footer", "baz");
    const http_response& cref = resp;
    LT_CHECK_EQ(cref.get_footer("x-footer"), std::string_view("baz"));
    LT_CHECK_EQ(cref.get_footer("X-FOOTER"), std::string_view("baz"));
LT_END_AUTO_TEST(footer_lookup_is_case_insensitive)

LT_BEGIN_AUTO_TEST(http_response_suite, cookie_lookup_is_case_insensitive)
    http_response resp = http_response::string("body");
    resp.with_cookie("SessionId", "token42");
    const http_response& cref = resp;
    LT_CHECK_EQ(cref.get_cookie("sessionid"), std::string_view("token42"));
    LT_CHECK_EQ(cref.get_cookie("SESSIONID"), std::string_view("token42"));
LT_END_AUTO_TEST(cookie_lookup_is_case_insensitive)

// View obtained after with_header replaces an existing key reflects the
// new value. We do NOT assert anything about the *old* view's validity —
// that would be testing undefined behaviour.
LT_BEGIN_AUTO_TEST(http_response_suite, get_header_view_reflects_replacement)
    http_response resp = http_response::string("body");
    resp.with_header("K", "v1");
    LT_CHECK_EQ(resp.get_header("K"), std::string_view("v1"));
    resp.with_header("K", "v2");
    // Also verify through a const reference (AC #3 for the replacement case).
    const http_response& cref = resp;
    LT_CHECK_EQ(cref.get_header("K"), std::string_view("v2"));
LT_END_AUTO_TEST(get_header_view_reflects_replacement)

// std::map node stability: adding a header for key B does NOT invalidate a
// previously-obtained view for key A (only same-key re-assignment does).
LT_BEGIN_AUTO_TEST(http_response_suite, get_header_view_stable_across_unrelated_mutation)
    http_response resp = http_response::string("body");
    resp.with_header("A", "alpha");
    std::string_view va = resp.get_header("A");
    // Mutate an unrelated key.
    resp.with_header("B", "beta");
    // View for A must still compare equal to its original value.
    LT_CHECK_EQ(va, std::string_view("alpha"));
LT_END_AUTO_TEST(get_header_view_stable_across_unrelated_mutation)

// -----------------------------------------------------------------------
// TASK-012: fluent with_* setters return http_response& / http_response&&
// (PRD-RSP-REQ-004).
// -----------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(http_response_suite, factory_chain_compiles_and_works)
    // Note: an SBO-inline counterpart of this chain exists in
    // http_response_factories_test.cpp (factory_chain_keeps_body_inline_in_sbo).
    // That test verifies the SBO invariant; this one pins the behavioral values.
    auto r = http_response::string("hi")
                 .with_header("X-Foo", "bar")
                 .with_status(201);
    LT_CHECK_EQ(r.get_status(), 201);
    LT_CHECK_EQ(r.get_header("X-Foo"), std::string_view("bar"));
    LT_CHECK_EQ(r.get_header("Content-Type"), std::string_view("text/plain"));
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(httpserver::body_kind::string));
LT_END_AUTO_TEST(factory_chain_compiles_and_works)

LT_BEGIN_AUTO_TEST(http_response_suite, lvalue_chain_returns_lvalue_ref)
    http_response r = http_response::empty();
    auto& ret = r.with_header("A", "1").with_footer("B", "2")
                  .with_cookie("c", "3").with_status(202);
    LT_CHECK_EQ(&ret, &r);  // Identity: returned ref must be *this.
    LT_CHECK_EQ(r.get_header("A"), std::string_view("1"));
    LT_CHECK_EQ(r.get_footer("B"), std::string_view("2"));
    LT_CHECK_EQ(r.get_cookie("c"), std::string_view("3"));
    LT_CHECK_EQ(r.get_status(), 202);
LT_END_AUTO_TEST(lvalue_chain_returns_lvalue_ref)

LT_BEGIN_AUTO_TEST(http_response_suite, with_setters_return_types_are_ref_qualified)
    using R = httpserver::http_response;
    // & overload returns R&
    static_assert(std::is_same_v<
        decltype(std::declval<R&>().with_header(std::string{}, std::string{})),
        R&>, "with_header() & must return http_response&");
    static_assert(std::is_same_v<
        decltype(std::declval<R&>().with_footer(std::string{}, std::string{})),
        R&>, "with_footer() & must return http_response&");
    static_assert(std::is_same_v<
        decltype(std::declval<R&>().with_cookie(std::string{}, std::string{})),
        R&>, "with_cookie() & must return http_response&");
    static_assert(std::is_same_v<
        decltype(std::declval<R&>().with_status(0)),
        R&>, "with_status() & must return http_response&");
    // && overload returns R&&
    static_assert(std::is_same_v<
        decltype(std::declval<R&&>().with_header(std::string{}, std::string{})),
        R&&>, "with_header() && must return http_response&&");
    static_assert(std::is_same_v<
        decltype(std::declval<R&&>().with_footer(std::string{}, std::string{})),
        R&&>, "with_footer() && must return http_response&&");
    static_assert(std::is_same_v<
        decltype(std::declval<R&&>().with_cookie(std::string{}, std::string{})),
        R&&>, "with_cookie() && must return http_response&&");
    static_assert(std::is_same_v<
        decltype(std::declval<R&&>().with_status(0)),
        R&&>, "with_status() && must return http_response&&");
    // Runtime check: verify that the lvalue & overload returns *this address.
    http_response r = http_response::empty();
    LT_CHECK_EQ(&r.with_header("A", "1"), &r);
LT_END_AUTO_TEST(with_setters_return_types_are_ref_qualified)

LT_BEGIN_AUTO_TEST(http_response_suite, statement_form_with_setters_still_compile)
    // Backward-compat: pre-TASK-012 callers wrote `r.with_X(k, v);` in
    // statement form, discarding the (then void) return. Switching to
    // a reference return must keep this form compiling unchanged.
    http_response resp = http_response::string("body");
    resp.with_header("X-A", "1");
    resp.with_footer("X-B", "2");
    resp.with_cookie("c", "3");
    resp.with_status(202);
    LT_CHECK_EQ(resp.get_header("X-A"), std::string_view("1"));
    LT_CHECK_EQ(resp.get_footer("X-B"), std::string_view("2"));
    LT_CHECK_EQ(resp.get_cookie("c"), std::string_view("3"));
    LT_CHECK_EQ(resp.get_status(), 202);
LT_END_AUTO_TEST(statement_form_with_setters_still_compile)

LT_BEGIN_AUTO_TEST(http_response_suite, with_status_changes_status_code)
    http_response r = http_response::string("body");
    LT_CHECK_EQ(r.get_status(), 200);  // factory default
    r.with_status(404);
    LT_CHECK_EQ(r.get_status(), 404);
    r.with_status(500);
    LT_CHECK_EQ(r.get_status(), 500);
LT_END_AUTO_TEST(with_status_changes_status_code)

LT_BEGIN_AUTO_TEST(http_response_suite, with_status_preserves_body_and_headers)
    auto r = http_response::string("payload", "application/json")
                 .with_header("X-K", "v")
                 .with_status(418);
    LT_CHECK_EQ(r.get_status(), 418);
    LT_CHECK_EQ(r.get_header("Content-Type"),
                std::string_view("application/json"));
    LT_CHECK_EQ(r.get_header("X-K"), std::string_view("v"));
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(httpserver::body_kind::string));
LT_END_AUTO_TEST(with_status_preserves_body_and_headers)

LT_BEGIN_AUTO_TEST(http_response_suite, mutation_observable_through_returned_ref)
    http_response r = http_response::empty();
    auto& ret = r.with_header("X-Trace", "a");
    LT_CHECK_EQ(ret.get_header("X-Trace"), std::string_view("a"));
    // And the rvalue chain leaves the result in the bound variable.
    auto r2 = http_response::empty().with_header("X-Trace", "b");
    LT_CHECK_EQ(r2.get_header("X-Trace"), std::string_view("b"));
LT_END_AUTO_TEST(mutation_observable_through_returned_ref)

LT_BEGIN_AUTO_TEST(http_response_suite, with_header_moves_string_args)
    // By-value string parameters must accept rvalue inputs and forward
    // them into the underlying map. We don't assert on the moved-from
    // state of the source strings (the standard only guarantees "valid
    // but unspecified") — only that the value lands in the map intact.
    http_response r = http_response::empty();
    std::string key = "X-Long-Header-Name-To-Avoid-SSO";
    std::string value(64, 'v');  // > SSO threshold on libstdc++/libc++
    const std::string expected(64, 'v');
    r.with_header(std::move(key), std::move(value));
    LT_CHECK_EQ(r.get_header("X-Long-Header-Name-To-Avoid-SSO"),
                std::string_view(expected));
LT_END_AUTO_TEST(with_header_moves_string_args)

// -----------------------------------------------------------------------
// TASK-012 review-pass: security validation on fluent setters.
//
// with_header, with_footer, with_cookie must reject keys/values that
// contain CR (\r), LF (\n), or NUL (\0) — these characters allow
// HTTP response-header injection (CWE-113). with_status must reject
// codes outside [100, 599] per RFC 9110 §15.
//
// The LT_CHECK_THROW / LT_CHECK_NOTHROW macros from littletest.hpp are
// used here in preference to manual bool threw = false; try { ... }
// catch { threw = true; } patterns. The macros are immune to accidental
// removal of the catch block and express intent in a single line.
// -----------------------------------------------------------------------

LT_BEGIN_AUTO_TEST(http_response_suite, with_header_rejects_crlf_in_value)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_header("X-Foo", "bar\r\nSet-Cookie: evil=1"));
LT_END_AUTO_TEST(with_header_rejects_crlf_in_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_header_rejects_lf_in_value)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_header("X-Foo", "bar\ninjected"));
LT_END_AUTO_TEST(with_header_rejects_lf_in_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_header_rejects_nul_in_value)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_header("X-Foo", std::string("bar\0baz", 7)));
LT_END_AUTO_TEST(with_header_rejects_nul_in_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_header_rejects_crlf_in_key)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_header("X-Foo\r\nEvil", "value"));
LT_END_AUTO_TEST(with_header_rejects_crlf_in_key)

LT_BEGIN_AUTO_TEST(http_response_suite, with_header_rejects_nul_in_key)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_header(std::string("X\0Y", 3), "value"));
LT_END_AUTO_TEST(with_header_rejects_nul_in_key)

LT_BEGIN_AUTO_TEST(http_response_suite, with_footer_rejects_crlf_in_value)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_footer("X-Footer", "val\r\ninjected"));
LT_END_AUTO_TEST(with_footer_rejects_crlf_in_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_footer_rejects_lf_in_key)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_footer("X-Footer\nEvil", "value"));
LT_END_AUTO_TEST(with_footer_rejects_lf_in_key)

LT_BEGIN_AUTO_TEST(http_response_suite, with_footer_rejects_nul_in_value)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_footer("X-Footer", std::string("val\0ue", 6)));
LT_END_AUTO_TEST(with_footer_rejects_nul_in_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_cookie_rejects_crlf_in_value)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_cookie("sid", "abc\r\nSet-Cookie: evil=1"));
LT_END_AUTO_TEST(with_cookie_rejects_crlf_in_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_cookie_rejects_lf_in_name)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_cookie("sid\nevil", "value"));
LT_END_AUTO_TEST(with_cookie_rejects_lf_in_name)

LT_BEGIN_AUTO_TEST(http_response_suite, with_cookie_rejects_nul_in_value)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_cookie("sid", std::string("abc\0def", 7)));
LT_END_AUTO_TEST(with_cookie_rejects_nul_in_value)

// with_status boundary: 99 is the meaningful probe for the lower bound.
// Negative values also fall below 100 but exercise no additional branch.
LT_BEGIN_AUTO_TEST(http_response_suite, with_status_rejects_below_100)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_status(99));
LT_END_AUTO_TEST(with_status_rejects_below_100)

LT_BEGIN_AUTO_TEST(http_response_suite, with_status_rejects_above_599)
    http_response resp = http_response::string("body");
    LT_CHECK_THROW(resp.with_status(600));
LT_END_AUTO_TEST(with_status_rejects_above_599)

LT_BEGIN_AUTO_TEST(http_response_suite, with_status_accepts_boundary_100)
    http_response resp = http_response::string("body");
    LT_CHECK_NOTHROW(resp.with_status(100));
    LT_CHECK_EQ(resp.get_status(), 100);
LT_END_AUTO_TEST(with_status_accepts_boundary_100)

LT_BEGIN_AUTO_TEST(http_response_suite, with_status_accepts_boundary_599)
    http_response resp = http_response::string("body");
    LT_CHECK_NOTHROW(resp.with_status(599));
    LT_CHECK_EQ(resp.get_status(), 599);
LT_END_AUTO_TEST(with_status_accepts_boundary_599)

LT_BEGIN_AUTO_TEST(http_response_suite, with_header_accepts_valid_value)
    http_response resp = http_response::string("body");
    LT_CHECK_NOTHROW(resp.with_header("X-Foo", "valid value with spaces and colons: ok"));
    LT_CHECK_EQ(resp.get_header("X-Foo"),
                std::string_view("valid value with spaces and colons: ok"));
LT_END_AUTO_TEST(with_header_accepts_valid_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_footer_accepts_valid_value)
    http_response resp = http_response::string("body");
    LT_CHECK_NOTHROW(resp.with_footer("X-Footer", "valid-footer-value"));
    LT_CHECK_EQ(resp.get_footer("X-Footer"),
                std::string_view("valid-footer-value"));
LT_END_AUTO_TEST(with_footer_accepts_valid_value)

LT_BEGIN_AUTO_TEST(http_response_suite, with_cookie_accepts_valid_value)
    http_response resp = http_response::string("body");
    LT_CHECK_NOTHROW(resp.with_cookie("sid", "abc123"));
    LT_CHECK_EQ(resp.get_cookie("sid"), std::string_view("abc123"));
LT_END_AUTO_TEST(with_cookie_accepts_valid_value)

LT_BEGIN_AUTO_TEST(http_response_suite, rvalue_chain_with_footer_and_cookie)
    // Pin the && overloads of with_footer and with_cookie in an rvalue chain.
    auto r = http_response::empty()
                 .with_footer("X-F", "fval")
                 .with_cookie("c", "cval");
    LT_CHECK_EQ(r.get_footer("X-F"), std::string_view("fval"));
    LT_CHECK_EQ(r.get_cookie("c"), std::string_view("cval"));
LT_END_AUTO_TEST(rvalue_chain_with_footer_and_cookie)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
