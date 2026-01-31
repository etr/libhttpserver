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

#include <string>

#include "./littletest.hpp"
#include "./httpserver.hpp"

using std::string;
using httpserver::http_response;
using httpserver::string_response;

LT_BEGIN_SUITE(http_response_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_response_suite)

LT_BEGIN_AUTO_TEST(http_response_suite, default_response_code)
    http_response resp;
    LT_CHECK_EQ(resp.get_response_code(), -1);
LT_END_AUTO_TEST(default_response_code)

LT_BEGIN_AUTO_TEST(http_response_suite, custom_response_code)
    http_response resp(404, "text/plain");
    LT_CHECK_EQ(resp.get_response_code(), 404);
LT_END_AUTO_TEST(custom_response_code)

LT_BEGIN_AUTO_TEST(http_response_suite, string_response_code)
    string_response resp("Not Found", 404, "text/plain");
    LT_CHECK_EQ(resp.get_response_code(), 404);
LT_END_AUTO_TEST(string_response_code)

LT_BEGIN_AUTO_TEST(http_response_suite, header_operations)
    http_response resp(200, "text/plain");
    resp.with_header("X-Custom-Header", "HeaderValue");
    LT_CHECK_EQ(resp.get_header("X-Custom-Header"), "HeaderValue");
LT_END_AUTO_TEST(header_operations)

LT_BEGIN_AUTO_TEST(http_response_suite, footer_operations)
    http_response resp(200, "text/plain");
    resp.with_footer("X-Footer", "FooterValue");
    LT_CHECK_EQ(resp.get_footer("X-Footer"), "FooterValue");
LT_END_AUTO_TEST(footer_operations)

LT_BEGIN_AUTO_TEST(http_response_suite, cookie_operations)
    http_response resp(200, "text/plain");
    resp.with_cookie("SessionId", "abc123");
    LT_CHECK_EQ(resp.get_cookie("SessionId"), "abc123");
LT_END_AUTO_TEST(cookie_operations)

LT_BEGIN_AUTO_TEST(http_response_suite, get_headers)
    http_response resp(200, "text/plain");
    resp.with_header("Header1", "Value1");
    resp.with_header("Header2", "Value2");
    auto headers = resp.get_headers();
    LT_CHECK_EQ(headers.at("Header1"), "Value1");
    LT_CHECK_EQ(headers.at("Header2"), "Value2");
LT_END_AUTO_TEST(get_headers)

LT_BEGIN_AUTO_TEST(http_response_suite, get_footers)
    http_response resp(200, "text/plain");
    resp.with_footer("Footer1", "Value1");
    resp.with_footer("Footer2", "Value2");
    auto footers = resp.get_footers();
    LT_CHECK_EQ(footers.at("Footer1"), "Value1");
    LT_CHECK_EQ(footers.at("Footer2"), "Value2");
LT_END_AUTO_TEST(get_footers)

LT_BEGIN_AUTO_TEST(http_response_suite, get_cookies)
    http_response resp(200, "text/plain");
    resp.with_cookie("Cookie1", "Value1");
    resp.with_cookie("Cookie2", "Value2");
    auto cookies = resp.get_cookies();
    LT_CHECK_EQ(cookies.at("Cookie1"), "Value1");
    LT_CHECK_EQ(cookies.at("Cookie2"), "Value2");
LT_END_AUTO_TEST(get_cookies)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
