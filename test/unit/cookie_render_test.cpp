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
LT_END_AUTO_TEST(with_name_returns_lvalue_ref_on_lvalue)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_returns_rvalue_ref_on_rvalue)
    static_assert(std::is_same_v<decltype(cookie{}.with_name(std::string{})),
                                 cookie&&>,
                  "rvalue with_name must return cookie&&");
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

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_path_rejects_semicolon)
    // CWE-113: a semicolon in Path would inject synthetic attributes
    // into the Set-Cookie header.
    bool threw = false;
    try { cookie{}.with_path("/; Secure"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_path_rejects_semicolon)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_domain_rejects_semicolon)
    // CWE-113: a semicolon in Domain would inject synthetic attributes.
    bool threw = false;
    try { cookie{}.with_domain("example.com; Secure"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_domain_rejects_semicolon)

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

LT_BEGIN_AUTO_TEST(cookie_render_suite, roundtrip_expires_and_max_age_name_value)
    // A cookie with both time-related attributes: rendering then
    // splitting on the first ';' and re-parsing must recover the
    // original name=value pair (mirrors roundtrip_first_token_is_name_value
    // for the Expires + Max-Age attribute combination specifically).
    std::string out = cookie{}.with_name("sid").with_value("abc")
                          .with_expires(784111777).with_max_age(3600)
                          .to_set_cookie_header();
    auto semi = out.find(';');
    std::string token = (semi == std::string::npos)
                              ? out : out.substr(0, semi);
    auto parsed = cookie::parse_cookie_header(token);
    LT_CHECK_EQ(parsed.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(parsed[0].name(), std::string("sid"));
    LT_CHECK_EQ(parsed[0].value(), std::string("abc"));
LT_END_AUTO_TEST(roundtrip_expires_and_max_age_name_value)

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

// with_name("") is a two-stage contract: validate_name's byte-rejection
// loop has nothing to iterate over on an empty string, so the setter
// itself accepts an empty name without throwing. Rejection is deferred
// to to_set_cookie_header() (pinned above by render_throws_when_name_empty).
// This test pins the setter-level half of that contract.
LT_BEGIN_AUTO_TEST(cookie_render_suite, with_name_accepts_empty_string)
    bool threw = false;
    try {
        cookie c = cookie{}.with_name("");
        LT_CHECK_EQ(c.name(), std::string(""));
    }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(with_name_accepts_empty_string)

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

// ---------------- Cycle 7: render-time injection guard (TASK-064 / security-reviewer-iter2-1) ----------------

// parse_cookie_header() deliberately bypasses validators, so a cookie
// object returned by it may carry a name containing bytes forbidden
// for Set-Cookie headers.  to_set_cookie_header() must detect this and
// throw rather than silently emit an injected header.
//
// Note: parse_cookie_header splits on ';', so no cookie object produced
// by it will ever have a ';' in its name — the parser always handles that
// before any cookie object is constructed. The render guard for ';' in
// name_ is therefore structurally unreachable via parse_cookie_header and
// only reachable via direct field injection (not via the normal API). The
// test below pins that parser property mechanically; the space (0x20)
// test after it exercises the render-guard code path itself and acts as
// the mechanical pin for the guard contract.

// Honest replacement for the former disjunctive
// render_throws_when_parsed_name_contains_semicolon test (which passed
// tautologically): assert the parser behaviour directly. Feeding
// "a;Secure=1=x" — an attempt to smuggle ';' into a name — the parser
// splits the header on ';' FIRST (src/cookie.cpp,
// cookie::parse_cookie_header: header_value.find(';', pos)), drops the
// "a" token (no '='), and parses "Secure=1=x" as name="Secure",
// value="1=x". A parsed cookie name can therefore never contain ';'.
LT_BEGIN_AUTO_TEST(cookie_render_suite, parser_splits_on_semicolon_so_name_cannot_contain_it)
    auto cookies = cookie::parse_cookie_header("a;Secure=1=x");
    LT_CHECK_EQ(cookies.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(cookies[0].name(), std::string("Secure"));
    LT_CHECK_EQ(cookies[0].value(), std::string("1=x"));
    LT_CHECK_EQ(cookies[0].name().find(';') == std::string::npos, true);
LT_END_AUTO_TEST(parser_splits_on_semicolon_so_name_cannot_contain_it)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_guard_rejects_space_in_parsed_name)
    // Directly manipulate via the raw parse path: inject a cookie name with
    // a space (0x20), which is rejected by is_invalid_name_byte but NOT split
    // on by the parser's semicolon delimiter.
    //
    // Parser behaviour relied upon (src/cookie.cpp): trim_ws() strips only
    // ' ' and '\t' from BOTH ENDS of each ';'-delimited token
    // (`tok = trim_ws(tok)`) and again from the ends of the name portion
    // after splitting at the first '=' (`name_sv = trim_ws(name_sv)`).
    // Interior bytes are never touched, so "a b=val" yields the name
    // "a b" with its interior space preserved. If trim_ws ever starts
    // collapsing interior whitespace, the precondition checks below fail
    // and this test must be updated.
    auto cookies = cookie::parse_cookie_header("a b=val");

    // Precondition: the parser must have produced a non-empty result and the
    // first cookie's name must contain the space for the render-guard test to
    // be meaningful. If either precondition fails, the parser semantics have
    // changed and this test needs to be updated.
    LT_CHECK_EQ(cookies.empty(), false);
    LT_CHECK_EQ(cookies[0].name().find(' ') != std::string::npos, true);

    // With the precondition satisfied, the render-time guard must fire.
    bool threw = false;
    try {
        std::string s = cookies[0].to_set_cookie_header();
        (void)s;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(render_guard_rejects_space_in_parsed_name)

// ---------------- Cycle 7: comma rejection in validate_attr_param (security-reviewer-iter2-2) ----------------

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_domain_rejects_comma)
    // HTTP/1.0 legacy parsers split Set-Cookie on commas, so a Domain
    // value containing a comma can produce a split-header injection.
    bool threw = false;
    try { cookie{}.with_domain("evil.com,other.com"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_domain_rejects_comma)

LT_BEGIN_AUTO_TEST(cookie_render_suite, with_path_rejects_comma)
    bool threw = false;
    try { cookie{}.with_path("/foo,/bar"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_path_rejects_comma)

// ---------------- Cycle 8: render-time value_ guard (security-reviewer-iter3-1) ----------------
// parse_cookie_header() stores value_ raw (bypassing validate_value()).
// to_set_cookie_header() must detect forbidden bytes in value_ at render
// time and throw, matching the existing name_ guard (CWE-113 defence-in-depth).
// Tests pin CR, LF, and NUL in a reflected parsed cookie value; ';' cannot
// reach value_ via the parse path (see with_value_rejects_semicolon_setter_path).

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_guard_rejects_cr_in_parsed_value)
    // Simulate a reflected parsed cookie: parse_cookie_header stores value_ raw.
    // Inject a CR byte via the raw parse path (not via with_value which rejects it).
    auto cookies = cookie::parse_cookie_header(std::string("sid=ab\rcd", 9));
    LT_CHECK_EQ(cookies.empty(), false);
    LT_CHECK_EQ(cookies[0].name(), std::string("sid"));
    // The raw value_ contains CR; to_set_cookie_header must throw.
    bool threw = false;
    try {
        std::string s = cookies[0].to_set_cookie_header();
        (void)s;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(render_guard_rejects_cr_in_parsed_value)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_guard_rejects_lf_in_parsed_value)
    auto cookies = cookie::parse_cookie_header(std::string("sid=ab\ncd", 9));
    LT_CHECK_EQ(cookies.empty(), false);
    LT_CHECK_EQ(cookies[0].name(), std::string("sid"));
    bool threw = false;
    try {
        std::string s = cookies[0].to_set_cookie_header();
        (void)s;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(render_guard_rejects_lf_in_parsed_value)

LT_BEGIN_AUTO_TEST(cookie_render_suite, render_guard_rejects_nul_in_parsed_value)
    auto cookies = cookie::parse_cookie_header(std::string("sid=ab\x00" "cd", 9));
    LT_CHECK_EQ(cookies.empty(), false);
    LT_CHECK_EQ(cookies[0].name(), std::string("sid"));
    bool threw = false;
    try {
        std::string s = cookies[0].to_set_cookie_header();
        (void)s;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(render_guard_rejects_nul_in_parsed_value)

// Setter-path pin only — NOT render-guard coverage. parse_cookie_header
// splits the whole header on ';' before extracting any value, so no
// parsed cookie can carry ';' in value_: the render guard's ';' branch is
// unreachable via the public API and is covered by inspection (the same
// guard loop is exercised by the CR/LF/NUL tests above). What CAN be
// pinned mechanically is the setter: with_value must reject ';'.
LT_BEGIN_AUTO_TEST(cookie_render_suite, with_value_rejects_semicolon_setter_path)
    bool threw = false;
    try { cookie{}.with_name("sid").with_value("ab;cd"); }
    catch (const std::invalid_argument&) { threw = true; }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(with_value_rejects_semicolon_setter_path)

// ---------------- Cycle 8: same_site=None + secure=true no-double-emit (test-quality-iter3-3) ----------------

LT_BEGIN_AUTO_TEST(cookie_render_suite, same_site_none_with_explicit_secure_emits_single_secure_token)
    // SameSite=None auto-coerces Secure=true (browser requirement).
    // When the caller also explicitly sets secure(true), the effective_secure
    // flag is still true (secure_ || (same_site_ == none)), but the renderer
    // must NOT emit "; Secure" twice. This test pins that the short-circuit
    // produces exactly one "; Secure" token.
    std::string out = cookie{}.with_name("sid").with_value("x")
                         .with_secure(true).with_same_site(same_site_mode::none)
                         .to_set_cookie_header();
    // The exact-string equality above already fully pins the output --
    // if it matches, "; Secure" occurs exactly once by definition, so a
    // separate occurrence-count loop would be dead confirmation.
    LT_CHECK_EQ(out, std::string("sid=x; Secure; SameSite=None"));
LT_END_AUTO_TEST(same_site_none_with_explicit_secure_emits_single_secure_token)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
