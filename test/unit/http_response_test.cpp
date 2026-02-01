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

LT_BEGIN_AUTO_TEST(http_response_suite, shoutcast_response)
    string_response resp("OK", 200, "audio/mpeg");
    int original_code = resp.get_response_code();
    resp.shoutCAST();
    // shoutCAST sets the MHD_ICY_FLAG (1 << 31) on response_code
    // Verify the flag bit is set (use unsigned comparison)
    LT_CHECK_EQ(static_cast<unsigned int>(resp.get_response_code()) & 0x80000000u, 0x80000000u);
    // Also verify the original code bits are preserved
    LT_CHECK_EQ(resp.get_response_code() & 0x7FFFFFFF, original_code);
LT_END_AUTO_TEST(shoutcast_response)

LT_BEGIN_AUTO_TEST(http_response_suite, string_response_default_constructor)
    string_response resp;
    // Default constructor should create response with default values
    LT_CHECK_EQ(resp.get_response_code(), -1);
LT_END_AUTO_TEST(string_response_default_constructor)

LT_BEGIN_AUTO_TEST(http_response_suite, string_response_content_only)
    string_response resp("Hello World");
    // Should use default response code (200) and content type (text/plain)
    LT_CHECK_EQ(resp.get_response_code(), 200);
LT_END_AUTO_TEST(string_response_content_only)

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
    http_response resp(201, "application/json");
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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
