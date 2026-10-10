/* Copyright (C) 2011-2026 Sebastiano Merlino; LGPL-2.1-or-later */
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <httpserver/http/cookie.hpp>
#include "./littletest.hpp"

namespace http = httpserver::http;

LT_BEGIN_SUITE(http_cookie_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(http_cookie_suite)

LT_BEGIN_AUTO_TEST(http_cookie_suite, parse_multi_semicolon_separated)
    const auto values = http::parse_cookie_header("a=1; b=2; c=3");
    LT_ASSERT_EQ(values.size(), std::size_t{3});
    for (std::size_t i = 0; i < values.size(); ++i) {
        LT_CHECK_EQ(values[i].name, std::string(1, 'a' + i));
        LT_CHECK_EQ(values[i].value, std::to_string(i + 1));
    }
LT_END_AUTO_TEST(parse_multi_semicolon_separated)

LT_BEGIN_AUTO_TEST(http_cookie_suite, render_full_attribute_ordering)
    http::cookie c{"sid", "abc", "example.com", "/", 784111777, 3600,
                   true, true, http::same_site_mode::strict};
    std::string output;
    LT_ASSERT(http::render_set_cookie(c, output).ok());
    LT_CHECK_EQ(output, std::string("sid=abc; Expires=Sun, 06 Nov 1994 08:49:37 GMT; "
        "Max-Age=3600; Domain=example.com; Path=/; Secure; HttpOnly; SameSite=Strict"));
LT_END_AUTO_TEST(render_full_attribute_ordering)

LT_BEGIN_AUTO_TEST(http_cookie_suite, render_guard_rejects_cr_in_parsed_value)
    const auto values = http::parse_cookie_header("sid=ab\rcd");
    LT_ASSERT_EQ(values.size(), std::size_t{1});
    LT_CHECK_EQ(values[0].name, std::string("sid"));
    LT_CHECK_EQ(values[0].value, std::string("ab\rcd"));
    std::string output = "unchanged";
    LT_CHECK(http::render_set_cookie(values[0], output).code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(output, std::string("unchanged"));
LT_END_AUTO_TEST(render_guard_rejects_cr_in_parsed_value)

LT_BEGIN_AUTO_TEST(http_cookie_suite, parser_preserves_legacy_token_rules)
    const auto values = http::parse_cookie_header("\t SID =\"a=b%20c\" \t; sid=; broken; =bad; \t =bad; q= leading; u=\"one");
    LT_ASSERT_EQ(values.size(), std::size_t{4});
    LT_CHECK_EQ(values[0].name, std::string("SID"));
    LT_CHECK_EQ(values[0].value, std::string("a=b%20c"));
    LT_CHECK_EQ(values[1].name, std::string("sid"));
    LT_CHECK(values[1].value.empty());
    LT_CHECK_EQ(values[2].value, std::string(" leading"));
    LT_CHECK_EQ(values[3].value, std::string("\"one"));
    LT_CHECK(http::parse_cookie_header(" ;\t;broken").empty());
    const auto split = http::parse_cookie_header("a;Secure=1=x");
    LT_ASSERT_EQ(split.size(), std::size_t{1});
    LT_CHECK_EQ(split[0].name, std::string("Secure"));
    LT_CHECK_EQ(split[0].value, std::string("1=x"));
LT_END_AUTO_TEST(parser_preserves_legacy_token_rules)

LT_BEGIN_AUTO_TEST(http_cookie_suite, parsed_storage_owns_input_and_roundtrips)
    std::vector<http::cookie> values;
    { std::string input = "SID=31d4d96e407aad42; b=b=c";
      values = http::parse_cookie_header(input);
      input.assign(input.size(), 'x'); }
    LT_ASSERT_EQ(values.size(), std::size_t{2});
    auto copy = values[0];
    values.clear();
    copy.path = "/"; copy.secure = true; copy.http_only = true;
    std::string output;
    LT_ASSERT(http::render_set_cookie(copy, output).ok());
    LT_CHECK_EQ(output, std::string("SID=31d4d96e407aad42; Path=/; Secure; HttpOnly"));
    const auto again = http::parse_cookie_header(output.substr(0, output.find(';')));
    LT_ASSERT_EQ(again.size(), std::size_t{1});
    LT_CHECK_EQ(again[0].name, copy.name);
    LT_CHECK_EQ(again[0].value, copy.value);
LT_END_AUTO_TEST(parsed_storage_owns_input_and_roundtrips)

LT_BEGIN_AUTO_TEST(http_cookie_suite, raw_mutation_and_parsing_cannot_bypass_injection_guard)
    for (const char byte : {'\r', '\n', '\0', ';'}) {
        const std::string bad = std::string("a") + byte + "b";
        for (int field = 0; field < 4; ++field) {
            http::cookie c{"sid", "ok"};
            if (field == 0) c.name = bad;
            if (field == 1) c.value = bad;
            if (field == 2) c.domain = bad;
            if (field == 3) c.path = bad;
            std::string output = "sentinel";
            LT_CHECK(http::render_set_cookie(c, output).code() == http::outcome_code::invalid_argument);
            LT_CHECK_EQ(output, std::string("sentinel"));
        }
        if (byte != ';') {
            const auto raw = http::parse_cookie_header("sid=" + bad);
            LT_ASSERT_EQ(raw.size(), std::size_t{1});
            LT_CHECK_EQ(raw[0].value, bad);
            std::string output;
            LT_CHECK(!http::render_set_cookie(raw[0], output).ok());
            LT_CHECK(output.empty());
        }
    }
LT_END_AUTO_TEST(raw_mutation_and_parsing_cannot_bypass_injection_guard)

LT_BEGIN_AUTO_TEST(http_cookie_suite, name_attribute_and_enum_validation_is_atomic)
    for (const auto& bad : {"", "a b", "a\tb", "a=b", "a\vb", "a\x7f"}) {
        http::cookie c{bad, "x"}; std::string output = "sentinel";
        LT_CHECK(http::render_set_cookie(c, output).code() == http::outcome_code::invalid_argument);
        LT_CHECK_EQ(output, std::string("sentinel"));
    }
    for (const bool domain : {false, true}) {
        http::cookie c{"a", "x"};
        (domain ? c.domain : c.path) = "bad,other";
        std::string output = "sentinel";
        LT_CHECK(!http::render_set_cookie(c, output).ok());
        LT_CHECK_EQ(output, std::string("sentinel"));
    }
    http::cookie c{"a", "x"};
    c.same_site = static_cast<http::same_site_mode>(255);
    std::string output = "sentinel";
    LT_CHECK(!http::render_set_cookie(c, output).ok());
    LT_CHECK_EQ(output, std::string("sentinel"));
LT_END_AUTO_TEST(name_attribute_and_enum_validation_is_atomic)

LT_BEGIN_AUTO_TEST(http_cookie_suite, signed_max_age_and_same_site_modes)
    for (const auto age : {std::int64_t{0}, std::int64_t{-1}, std::numeric_limits<std::int64_t>::min()}) {
        http::cookie c{"a", "b=c%20d"}; c.max_age = age;
        std::string output;
        LT_ASSERT(http::render_set_cookie(c, output).ok());
        LT_CHECK_EQ(output, "a=b=c%20d; Max-Age=" + std::to_string(age));
    }
    for (const auto mode : {http::same_site_mode::unset, http::same_site_mode::strict,
                            http::same_site_mode::lax, http::same_site_mode::none}) {
        http::cookie c{"a", ""}; c.same_site = mode;
        std::string output;
        LT_ASSERT(http::render_set_cookie(c, output).ok());
        const char* suffix = mode == http::same_site_mode::strict ? "; SameSite=Strict" :
            mode == http::same_site_mode::lax ? "; SameSite=Lax" :
            mode == http::same_site_mode::none ? "; Secure; SameSite=None" : "";
        LT_CHECK_EQ(output, std::string("a=") + suffix);
        LT_CHECK(!c.secure);
    }
LT_END_AUTO_TEST(signed_max_age_and_same_site_modes)

LT_BEGIN_AUTO_TEST(http_cookie_suite, expires_is_bounded_civil_gmt)
    const std::vector<std::pair<std::int64_t, std::string>> dates = {
        {0, "Thu, 01 Jan 1970 00:00:00 GMT"}, {-1, "Wed, 31 Dec 1969 23:59:59 GMT"},
        {951782400, "Tue, 29 Feb 2000 00:00:00 GMT"},
        {-62167219200, "Sat, 01 Jan 0000 00:00:00 GMT"},
        {253402300799, "Fri, 31 Dec 9999 23:59:59 GMT"}};
    for (const auto& [epoch, expected] : dates) {
        http::cookie c{"a", "b"}; c.expires = epoch;
        std::string output;
        LT_ASSERT(http::render_set_cookie(c, output).ok());
        LT_CHECK_EQ(output, "a=b; Expires=" + expected);
    }
    for (const auto epoch : {std::int64_t{-62167219201}, std::int64_t{253402300800},
                            std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
        http::cookie c{"a", "b"}; c.expires = epoch;
        std::string output = "sentinel";
        LT_CHECK(http::render_set_cookie(c, output).code() == http::outcome_code::invalid_argument);
        LT_CHECK_EQ(output, std::string("sentinel"));
    }
LT_END_AUTO_TEST(expires_is_bounded_civil_gmt)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
