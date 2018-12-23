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

#if defined(__MINGW32__) || defined(__CYGWIN32__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include "littletest.hpp"
#include "http_utils.hpp"

#include <cstdio>

using namespace httpserver;
using namespace std;

LT_BEGIN_SUITE(http_utils_suite)
    void set_up()
    {
    }

    void tear_down()
    {
    }
LT_END_SUITE(http_utils_suite)

LT_BEGIN_AUTO_TEST(http_utils_suite, unescape)
    char* string_with_plus = (char*) malloc(6 * sizeof(char));
    sprintf(string_with_plus, "%s", "A%20B");
    int expected_size = http::http_unescape(string_with_plus);

    char* expected = (char*) malloc(4 * sizeof(char));
    sprintf(expected, "%s", "A B");

    LT_CHECK_EQ(string(string_with_plus), string(expected));
    LT_CHECK_EQ(expected_size, 3);
LT_END_AUTO_TEST(unescape)

LT_BEGIN_AUTO_TEST(http_utils_suite, standardize_url)
    string url = "/", result;
    result = http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/");

    url = "/abc/", result = "";
    result = http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc");

    url = "/abc", result = "";
    result = http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc");

    url = "/abc/pqr/", result = "";
    result = http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc/pqr");

    url = "/abc/pqr", result = "";
    result = http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc/pqr");
LT_END_AUTO_TEST(standardize_url)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    string result = http::get_ip_str((struct sockaddr*) &ip4addr);
    unsigned short port = http::get_port((struct sockaddr*) &ip4addr);

    LT_CHECK_EQ(result, "127.0.0.1");
    LT_CHECK_EQ(port, htons(3490));
LT_END_AUTO_TEST(ip_to_str)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str6)
    struct sockaddr_in6 ip6addr;

    ip6addr.sin6_family = AF_INET6;
    ip6addr.sin6_port = htons(3490);
    inet_pton(AF_INET6, "2001:db8:8714:3a90::12", &(ip6addr.sin6_addr));

    string result = http::get_ip_str((struct sockaddr *) &ip6addr);
    unsigned short port = http::get_port((struct sockaddr*) &ip6addr);

    LT_CHECK_EQ(result, "2001:db8:8714:3a90::12");
    LT_CHECK_EQ(port, htons(3490));
LT_END_AUTO_TEST(ip_to_str6)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
