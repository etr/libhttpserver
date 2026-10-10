/* Copyright (C) 2011-2026 Sebastiano Merlino; LGPL-2.1-or-later */
#include <string>

#include <httpserver/http/cookie.hpp>
#include <httpserver/http.hpp>

#include "./littletest.hpp"

namespace http = httpserver::http;
LT_BEGIN_SUITE(consumer_cookie_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(consumer_cookie_suite)

LT_BEGIN_AUTO_TEST(consumer_cookie_suite, installed_cookie_operations_preserve_repeated_fields)
    http::fields request;
    request.append("Cookie", "SID=one; empty=");
    request.append("Cookie", "SID=two%20three");
    http::fields response;
    for (const auto& field : request.all("cookie")) {
        for (auto value : http::parse_cookie_header(field)) {
            value.same_site = http::same_site_mode::none;
            std::string rendered;
            LT_ASSERT(http::render_set_cookie(value, rendered).ok());
            response.append("Set-Cookie", rendered);
        }
    }
    const auto values = response.all("set-cookie");
    LT_ASSERT_EQ(values.size(), std::size_t{3});
    LT_CHECK_EQ(values[0], std::string("SID=one; Secure; SameSite=None"));
    LT_CHECK_EQ(values[1], std::string("empty=; Secure; SameSite=None"));
    LT_CHECK_EQ(values[2], std::string("SID=two%20three; Secure; SameSite=None"));
    const auto unsafe = http::parse_cookie_header("sid=a\rb");
    LT_ASSERT_EQ(unsafe.size(), std::size_t{1});
    std::string output = "unchanged";
    LT_CHECK(http::render_set_cookie(unsafe[0], output).code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(output, std::string("unchanged"));
LT_END_AUTO_TEST(installed_cookie_operations_preserve_repeated_fields)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
