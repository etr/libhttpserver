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

// TASK-064 Cycles 2-6: fluent setter shape, setter validation, RFC 6265
// §4.1 rendering, §5.4 parsing, and the round-trip pinning tests for
// httpserver::cookie. Pure CPU test -- no MHD.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::cookie;
using httpserver::same_site_mode;

LT_BEGIN_SUITE(cookie_render_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(cookie_render_suite)

// ---------------- Cycle 2: fluent setter ref-qualifier shape ----------------

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_returns_lvalue_ref_on_lvalue)
    cookie c;
    static_assert(std::is_same_v<decltype(c.with_name(std::string{})), cookie&>,
                  "lvalue with_name must return cookie&");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(with_name_returns_lvalue_ref_on_lvalue)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_returns_rvalue_ref_on_rvalue)
    static_assert(std::is_same_v<decltype(cookie{}.with_name(std::string{})),
                                 cookie&&>,
                  "rvalue with_name must return cookie&&");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(with_name_returns_rvalue_ref_on_rvalue)

LT_BEGIN_AUTO_TEST(cookie_render_suite, fluent_chain_round_trip_getters)
    cookie c = cookie{}
        .with_name("sid")
        .with_value("abc")
        .with_domain("example.com")
        .with_path("/")
        .with_secure(true)
        .with_http_only(true)
        .with_same_site(same_site_mode::strict)
        .with_max_age(3600)
        .with_expires(784111777);
    LT_CHECK_EQ(c.name(), std::string("sid"));
    LT_CHECK_EQ(c.value(), std::string("abc"));
    LT_CHECK_EQ(c.domain(), std::string("example.com"));
    LT_CHECK_EQ(c.path(), std::string("/"));
    LT_CHECK_EQ(c.is_secure(), true);
    LT_CHECK_EQ(c.is_http_only(), true);
    LT_CHECK_EQ(c.same_site() == same_site_mode::strict, true);
    LT_CHECK_EQ(c.max_age().value(), static_cast<std::int64_t>(3600));
    LT_CHECK_EQ(c.expires().value(), static_cast<std::int64_t>(784111777));
LT_END_AUTO_TEST(fluent_chain_round_trip_getters)

// ---------------- Cycle 3: setter validation ----------------

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_rejects_crlf)
    bool threw = false;
    try { cookie{}.with_name("a\r\nb"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_name_rejects_crlf)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_rejects_semicolon)
    bool threw = false;
    try { cookie{}.with_name("a;b"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_name_rejects_semicolon)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_rejects_equals)
    bool threw = false;
    try { cookie{}.with_name("a=b"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_name_rejects_equals)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_rejects_space)
    bool threw = false;
    try { cookie{}.with_name("a b"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_name_rejects_space)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_value_rejects_crlf)
    bool threw = false;
    try { cookie{}.with_value("a\r\nb"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_value_rejects_crlf)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_value_rejects_semicolon)
    bool threw = false;
    try { cookie{}.with_value("a;b"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_value_rejects_semicolon)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_value_rejects_nul)
    bool threw = false;
    std::string val("a");
    val.push_back('\0');
    val.append("b");
    try { cookie{}.with_value(val); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_value_rejects_nul)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_value_accepts_equals)
    // Cookie VALUES can legally contain '=' (e.g. base64 padding) -- only
    // ';' is rejected on the value side.
    cookie c = cookie{}.with_name("a").with_value("b=c=d");
    LT_CHECK_EQ(c.value(), std::string("b=c=d"));
LT_END_AUTO_TEST(with_value_accepts_equals)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_domain_rejects_crlf)
    bool threw = false;
    try { cookie{}.with_domain("ex\r\nample.com"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_domain_rejects_crlf)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_path_rejects_crlf)
    bool threw = false;
    try { cookie{}.with_path("/foo\r\nbar"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_path_rejects_crlf)

// ---------------- Cycle 4: to_set_cookie_header() rendering ----------------

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_minimal_name_value)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc")
                .to_set_cookie_header(),
                std::string("sid=abc"));
LT_END_AUTO_TEST(render_minimal_name_value)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_with_path)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc").with_path("/")
                .to_set_cookie_header(),
                std::string("sid=abc; Path=/"));
LT_END_AUTO_TEST(render_with_path)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_with_domain)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc")
                .with_domain("example.com").to_set_cookie_header(),
                std::string("sid=abc; Domain=example.com"));
LT_END_AUTO_TEST(render_with_domain)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_secure_flag_no_equals)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc").with_secure(true)
                .to_set_cookie_header(),
                std::string("sid=abc; Secure"));
LT_END_AUTO_TEST(render_secure_flag_no_equals)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_http_only_flag)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc").with_http_only(true)
                .to_set_cookie_header(),
                std::string("sid=abc; HttpOnly"));
LT_END_AUTO_TEST(render_http_only_flag)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_max_age_positive)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc").with_max_age(3600)
                .to_set_cookie_header(),
                std::string("sid=abc; Max-Age=3600"));
LT_END_AUTO_TEST(render_max_age_positive)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_max_age_zero)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc").with_max_age(0)
                .to_set_cookie_header(),
                std::string("sid=abc; Max-Age=0"));
LT_END_AUTO_TEST(render_max_age_zero)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_max_age_negative_preserved)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("abc").with_max_age(-1)
                .to_set_cookie_header(),
                std::string("sid=abc; Max-Age=-1"));
LT_END_AUTO_TEST(render_max_age_negative_preserved)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_expires_imf_fixdate)
    // Canonical RFC 6265 §4.1 example: 1994-11-06 08:49:37 GMT == epoch 784111777
    cookie c = cookie{}.with_name("sid").with_value("abc")
                    .with_expires(784111777);
    LT_CHECK_EQ(c.to_set_cookie_header(),
                std::string("sid=abc; Expires=Sun, 06 Nov 1994 08:49:37 GMT"));
LT_END_AUTO_TEST(render_expires_imf_fixdate)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_both_expires_and_max_age)
    std::string out = cookie{}.with_name("sid").with_value("abc")
                          .with_expires(784111777).with_max_age(3600)
                          .to_set_cookie_header();
    // Both attributes must appear, with Expires before Max-Age.
    auto expires_pos = out.find("Expires=");
    auto max_age_pos = out.find("Max-Age=");
    LT_CHECK_EQ(expires_pos != std::string::npos, true);
    LT_CHECK_EQ(max_age_pos != std::string::npos, true);
    LT_CHECK_EQ(expires_pos < max_age_pos, true);
LT_END_AUTO_TEST(render_both_expires_and_max_age)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_same_site_strict)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("x")
                .with_same_site(same_site_mode::strict).to_set_cookie_header(),
                std::string("sid=x; SameSite=Strict"));
LT_END_AUTO_TEST(render_same_site_strict)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_same_site_lax)
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("x")
                .with_same_site(same_site_mode::lax).to_set_cookie_header(),
                std::string("sid=x; SameSite=Lax"));
LT_END_AUTO_TEST(render_same_site_lax)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_same_site_unset_omits_attr)
    std::string out = cookie{}.with_name("sid").with_value("x")
                          .to_set_cookie_header();
    LT_CHECK_EQ(out.find("SameSite") == std::string::npos, true);
LT_END_AUTO_TEST(render_same_site_unset_omits_attr)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_same_site_none_forces_secure)
    // SameSite=None MUST appear with Secure, browser requirement.
    cookie c = cookie{}.with_name("sid").with_value("x")
                   .with_same_site(same_site_mode::none);
    LT_CHECK_EQ(c.is_secure(), false);          // user did not set it
    std::string out = c.to_set_cookie_header();
    LT_CHECK_EQ(out.find("; Secure") != std::string::npos, true);
    LT_CHECK_EQ(out.find("; SameSite=None") != std::string::npos, true);
LT_END_AUTO_TEST(render_same_site_none_forces_secure)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_full_attribute_ordering)
    // Order set in non-canonical sequence; renderer must canonicalize to:
    //   name=value; Expires=...; Max-Age=...; Domain=...; Path=...;
    //   Secure; HttpOnly; SameSite=Strict
    std::string out = cookie{}
        .with_same_site(same_site_mode::strict)
        .with_http_only(true)
        .with_secure(true)
        .with_path("/")
        .with_domain("example.com")
        .with_max_age(3600)
        .with_expires(784111777)
        .with_value("abc")
        .with_name("sid")
        .to_set_cookie_header();
    LT_CHECK_EQ(out,
        std::string("sid=abc"
                    "; Expires=Sun, 06 Nov 1994 08:49:37 GMT"
                    "; Max-Age=3600"
                    "; Domain=example.com"
                    "; Path=/"
                    "; Secure"
                    "; HttpOnly"
                    "; SameSite=Strict"));
LT_END_AUTO_TEST(render_full_attribute_ordering)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_value_byte_transparent)
    // No percent-decoding/encoding for the user.
    LT_CHECK_EQ(cookie{}.with_name("sid").with_value("a%20b")
                .to_set_cookie_header(),
                std::string("sid=a%20b"));
LT_END_AUTO_TEST(render_value_byte_transparent)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_throws_when_name_empty)
    bool threw = false;
    try {
        std::string s = cookie{}.with_value("x").to_set_cookie_header();
        (void)s;
    }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(render_throws_when_name_empty)

// ---------------- Cycle 5: parse_cookie_header() ----------------

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_single_name_value)
    auto v = cookie::parse_cookie_header("sid=abc");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("sid"));
    LT_CHECK_EQ(v[0].value(), std::string("abc"));
LT_END_AUTO_TEST(parse_single_name_value)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_multi_semicolon_separated)
    auto v = cookie::parse_cookie_header("a=1; b=2; c=3");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(3));
    LT_CHECK_EQ(v[0].name(), std::string("a"));
    LT_CHECK_EQ(v[0].value(), std::string("1"));
    LT_CHECK_EQ(v[1].name(), std::string("b"));
    LT_CHECK_EQ(v[1].value(), std::string("2"));
    LT_CHECK_EQ(v[2].name(), std::string("c"));
    LT_CHECK_EQ(v[2].value(), std::string("3"));
LT_END_AUTO_TEST(parse_multi_semicolon_separated)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_tolerates_no_space_after_semicolon)
    auto v = cookie::parse_cookie_header("a=1;b=2");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(v[0].name(), std::string("a"));
    LT_CHECK_EQ(v[1].name(), std::string("b"));
LT_END_AUTO_TEST(parse_tolerates_no_space_after_semicolon)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_tolerates_extra_whitespace)
    auto v = cookie::parse_cookie_header("  a=1  ;  b=2  ");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(v[0].name(), std::string("a"));
    LT_CHECK_EQ(v[0].value(), std::string("1"));
    LT_CHECK_EQ(v[1].name(), std::string("b"));
    LT_CHECK_EQ(v[1].value(), std::string("2"));
LT_END_AUTO_TEST(parse_tolerates_extra_whitespace)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_strips_outer_dquotes)
    auto v = cookie::parse_cookie_header("a=\"b\"");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].value(), std::string("b"));
LT_END_AUTO_TEST(parse_strips_outer_dquotes)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_preserves_inner_equals)
    auto v = cookie::parse_cookie_header("a=b=c");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("a"));
    LT_CHECK_EQ(v[0].value(), std::string("b=c"));
LT_END_AUTO_TEST(parse_preserves_inner_equals)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_empty_value_kept)
    auto v = cookie::parse_cookie_header("empty=");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("empty"));
    LT_CHECK_EQ(v[0].value(), std::string(""));
LT_END_AUTO_TEST(parse_empty_value_kept)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_skips_malformed_no_equals)
    auto v = cookie::parse_cookie_header("=novalue; a=1");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("a"));
    LT_CHECK_EQ(v[0].value(), std::string("1"));
LT_END_AUTO_TEST(parse_skips_malformed_no_equals)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_empty_header_empty_vector)
    auto v = cookie::parse_cookie_header("");
    LT_CHECK_EQ(v.empty(), true);
LT_END_AUTO_TEST(parse_empty_header_empty_vector)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_case_preserves_names)
    auto v = cookie::parse_cookie_header("SID=x; sid=y");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(v[0].name(), std::string("SID"));
    LT_CHECK_EQ(v[1].name(), std::string("sid"));
LT_END_AUTO_TEST(parse_case_preserves_names)

LT_BEGIN_AUTO_TEST(cookie_render_suite, parse_does_not_unescape)
    auto v = cookie::parse_cookie_header("a=b%20c");
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].value(), std::string("b%20c"));
LT_END_AUTO_TEST(parse_does_not_unescape)

// ---------------- Cycle 6: Round-trip ----------------

LT_BEGIN_AUTO_TEST(cookie_render_suite, roundtrip_rfc6265_example_4_1)
    // RFC 6265 §4.1 example:
    //   Set-Cookie: SID=31d4d96e407aad42; Path=/; Secure; HttpOnly
    cookie c = cookie{}
        .with_name("SID")
        .with_value("31d4d96e407aad42")
        .with_path("/")
        .with_secure(true)
        .with_http_only(true);
    std::string rendered = c.to_set_cookie_header();
    LT_CHECK_EQ(rendered,
                std::string("SID=31d4d96e407aad42; Path=/; Secure; HttpOnly"));

    // Parse the name=value pair as a request cookie (request Cookie:
    // headers carry no attributes per RFC 6265 §5.4 -- so we test the
    // bare name=value half).
    auto parsed = cookie::parse_cookie_header("SID=31d4d96e407aad42");
    LT_CHECK_EQ(parsed.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(parsed[0].name(), std::string("SID"));
    LT_CHECK_EQ(parsed[0].value(), std::string("31d4d96e407aad42"));
LT_END_AUTO_TEST(roundtrip_rfc6265_example_4_1)

LT_BEGIN_AUTO_TEST(cookie_render_suite, roundtrip_first_token_is_name_value)
    // Rendering then splitting on the first ';' must recover name=value.
    std::string out = cookie{}.with_name("sid").with_value("abc")
                          .with_path("/").with_secure(true)
                          .to_set_cookie_header();
    auto semi = out.find(';');
    std::string token = (semi == std::string::npos)
                              ? out : out.substr(0, semi);
    LT_CHECK_EQ(token, std::string("sid=abc"));
LT_END_AUTO_TEST(roundtrip_first_token_is_name_value)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
