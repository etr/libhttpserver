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

LT_BEGIN_AUTO_TEST(http_utils_suite, unescape_plus)
    char* string_with_plus = (char*) malloc(6 * sizeof(char));
    sprintf(string_with_plus, "%s", "A+B");
    int expected_size = http::http_unescape(string_with_plus);

    char* expected = (char*) malloc(4 * sizeof(char));
    sprintf(expected, "%s", "A B");

    LT_CHECK_EQ(string(string_with_plus), string(expected));
    LT_CHECK_EQ(expected_size, 3);
LT_END_AUTO_TEST(unescape_plus)

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

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str_invalid_family)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = 55;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    LT_CHECK_THROW(http::get_ip_str((struct sockaddr*) &ip4addr));
LT_END_AUTO_TEST(ip_to_str_invalid_family)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str_null)
    LT_CHECK_THROW(http::get_ip_str((struct sockaddr*) 0x0));
LT_END_AUTO_TEST(ip_to_str_null)

LT_BEGIN_AUTO_TEST(http_utils_suite, get_port_invalid_family)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = 55;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    LT_CHECK_THROW(http::get_port((struct sockaddr*) &ip4addr));
LT_END_AUTO_TEST(get_port_invalid_family)

LT_BEGIN_AUTO_TEST(http_utils_suite, get_port_null)
    LT_CHECK_THROW(http::get_port((struct sockaddr*) 0x0));
LT_END_AUTO_TEST(get_port_null)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_str)
    http::ip_representation test_ip("192.168.5.5");
    
    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV4);

    for (int i = 0; i < 12; i++) {
        LT_CHECK_EQ(test_ip.pieces[i], 0);
    }

    LT_CHECK_EQ(test_ip.pieces[12], 192);
    LT_CHECK_EQ(test_ip.pieces[13], 168);
    LT_CHECK_EQ(test_ip.pieces[14], 5);
    LT_CHECK_EQ(test_ip.pieces[15], 5);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);
LT_END_AUTO_TEST(ip_representation4_str)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_str_mask)
    http::ip_representation test_ip("192.168.*.*");
    
    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV4);

    for (int i = 0; i < 12; i++) {
        LT_CHECK_EQ(test_ip.pieces[i], 0);
    }

    LT_CHECK_EQ(test_ip.pieces[12], 192);
    LT_CHECK_EQ(test_ip.pieces[13], 168);
    LT_CHECK_EQ(test_ip.pieces[14], 0);
    LT_CHECK_EQ(test_ip.pieces[15], 0);

    LT_CHECK_EQ(test_ip.mask, 0x3FFF);
LT_END_AUTO_TEST(ip_representation4_str_mask)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_str_invalid)
    LT_CHECK_THROW(http::ip_representation("192.168.5.5.5"));
LT_END_AUTO_TEST(ip_representation4_str_invalid)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_str_beyond255)
    LT_CHECK_THROW(http::ip_representation("192.168.256.5"));
LT_END_AUTO_TEST(ip_representation4_str_beyond255)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str)
    http::ip_representation test_ip("2001:db8:8714:3a90::12");
    
    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 32);
    LT_CHECK_EQ(test_ip.pieces[1], 1);
    LT_CHECK_EQ(test_ip.pieces[2], 13);
    LT_CHECK_EQ(test_ip.pieces[3], 184);
    LT_CHECK_EQ(test_ip.pieces[4], 135);
    LT_CHECK_EQ(test_ip.pieces[5], 20);
    LT_CHECK_EQ(test_ip.pieces[6], 58);
    LT_CHECK_EQ(test_ip.pieces[7], 144);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 0);
    LT_CHECK_EQ(test_ip.pieces[11], 0);
    LT_CHECK_EQ(test_ip.pieces[12], 0);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 0);
    LT_CHECK_EQ(test_ip.pieces[15], 18);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);
LT_END_AUTO_TEST(ip_representation6_str)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_mask)
    http::ip_representation test_ip("2001:db8:8714:3a90:*:*");
    
    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 32);
    LT_CHECK_EQ(test_ip.pieces[1], 1);
    LT_CHECK_EQ(test_ip.pieces[2], 13);
    LT_CHECK_EQ(test_ip.pieces[3], 184);
    LT_CHECK_EQ(test_ip.pieces[4], 135);
    LT_CHECK_EQ(test_ip.pieces[5], 20);
    LT_CHECK_EQ(test_ip.pieces[6], 58);
    LT_CHECK_EQ(test_ip.pieces[7], 144);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 0);
    LT_CHECK_EQ(test_ip.pieces[11], 0);
    LT_CHECK_EQ(test_ip.pieces[12], 0);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 0);
    LT_CHECK_EQ(test_ip.pieces[15], 0);

    LT_CHECK_EQ(test_ip.mask, 0xF0FF);
LT_END_AUTO_TEST(ip_representation6_str_mask)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_nested)
    http::ip_representation test_ip("::ffff:192.0.2.128");

    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 0);
    LT_CHECK_EQ(test_ip.pieces[1], 0);
    LT_CHECK_EQ(test_ip.pieces[2], 0);
    LT_CHECK_EQ(test_ip.pieces[3], 0);
    LT_CHECK_EQ(test_ip.pieces[4], 0);
    LT_CHECK_EQ(test_ip.pieces[5], 0);
    LT_CHECK_EQ(test_ip.pieces[6], 0);
    LT_CHECK_EQ(test_ip.pieces[7], 0);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 255);
    LT_CHECK_EQ(test_ip.pieces[11], 255);
    LT_CHECK_EQ(test_ip.pieces[12], 192);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 2);
    LT_CHECK_EQ(test_ip.pieces[15], 128);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);
LT_END_AUTO_TEST(ip_representation6_str_nested)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_nested_deprecated)
    LT_CHECK_NOTHROW(http::ip_representation("::192.0.2.128"));
    http::ip_representation test_ip("::192.0.2.128");

    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 0);
    LT_CHECK_EQ(test_ip.pieces[1], 0);
    LT_CHECK_EQ(test_ip.pieces[2], 0);
    LT_CHECK_EQ(test_ip.pieces[3], 0);
    LT_CHECK_EQ(test_ip.pieces[4], 0);
    LT_CHECK_EQ(test_ip.pieces[5], 0);
    LT_CHECK_EQ(test_ip.pieces[6], 0);
    LT_CHECK_EQ(test_ip.pieces[7], 0);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 0);
    LT_CHECK_EQ(test_ip.pieces[11], 0);
    LT_CHECK_EQ(test_ip.pieces[12], 192);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 2);
    LT_CHECK_EQ(test_ip.pieces[15], 128);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);
LT_END_AUTO_TEST(ip_representation6_str_nested_deprecated)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_ipv4_mask)
    http::ip_representation test_ip("::ffff:192.0.*.*");

    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 0);
    LT_CHECK_EQ(test_ip.pieces[1], 0);
    LT_CHECK_EQ(test_ip.pieces[2], 0);
    LT_CHECK_EQ(test_ip.pieces[3], 0);
    LT_CHECK_EQ(test_ip.pieces[4], 0);
    LT_CHECK_EQ(test_ip.pieces[5], 0);
    LT_CHECK_EQ(test_ip.pieces[6], 0);
    LT_CHECK_EQ(test_ip.pieces[7], 0);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 255);
    LT_CHECK_EQ(test_ip.pieces[11], 255);
    LT_CHECK_EQ(test_ip.pieces[12], 192);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 0);
    LT_CHECK_EQ(test_ip.pieces[15], 0);

    LT_CHECK_EQ(test_ip.mask, 0x3FFF);
LT_END_AUTO_TEST(ip_representation6_str_ipv4_mask)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_clustered_middle)
    http::ip_representation test_ip("2001:db8::ff00:42:8329");

    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 32);
    LT_CHECK_EQ(test_ip.pieces[1], 1);
    LT_CHECK_EQ(test_ip.pieces[2], 13);
    LT_CHECK_EQ(test_ip.pieces[3], 184);
    LT_CHECK_EQ(test_ip.pieces[4], 0);
    LT_CHECK_EQ(test_ip.pieces[5], 0);
    LT_CHECK_EQ(test_ip.pieces[6], 0);
    LT_CHECK_EQ(test_ip.pieces[7], 0);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 255);
    LT_CHECK_EQ(test_ip.pieces[11], 0);
    LT_CHECK_EQ(test_ip.pieces[12], 0);
    LT_CHECK_EQ(test_ip.pieces[13], 66);
    LT_CHECK_EQ(test_ip.pieces[14], 131);
    LT_CHECK_EQ(test_ip.pieces[15], 41);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);
LT_END_AUTO_TEST(ip_representation6_str_clustered_middle)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_loopback)
    http::ip_representation test_ip("::1");

    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 0);
    LT_CHECK_EQ(test_ip.pieces[1], 0);
    LT_CHECK_EQ(test_ip.pieces[2], 0);
    LT_CHECK_EQ(test_ip.pieces[3], 0);
    LT_CHECK_EQ(test_ip.pieces[4], 0);
    LT_CHECK_EQ(test_ip.pieces[5], 0);
    LT_CHECK_EQ(test_ip.pieces[6], 0);
    LT_CHECK_EQ(test_ip.pieces[7], 0);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 0);
    LT_CHECK_EQ(test_ip.pieces[11], 0);
    LT_CHECK_EQ(test_ip.pieces[12], 0);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 0);
    LT_CHECK_EQ(test_ip.pieces[15], 1);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);
LT_END_AUTO_TEST(ip_representation6_str_loopback)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation_weight)
    LT_CHECK_EQ(http::ip_representation("::1").weight(), 16);
    LT_CHECK_EQ(http::ip_representation("192.168.0.1").weight(), 16);
    LT_CHECK_EQ(http::ip_representation("192.168.*.*").weight(), 14);
    LT_CHECK_EQ(http::ip_representation("::ffff:192.0.*.*").weight(), 14);
    LT_CHECK_EQ(http::ip_representation("2001:db8:8714:3a90:*:*").weight(), 12);
    LT_CHECK_EQ(http::ip_representation("2001:db8:8714:3a90:8714:2001:db8:3a90").weight(), 16);
    LT_CHECK_EQ(http::ip_representation("2001:db8:8714:3a90:8714:2001:*:*").weight(), 12);
    LT_CHECK_EQ(http::ip_representation("*:*:*:*:*:*:*:*").weight(), 0);
LT_END_AUTO_TEST(ip_representation_weight)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid)
    LT_CHECK_THROW(http::ip_representation("2001:db8:8714:3a90::12:4:4:4"));
LT_END_AUTO_TEST(ip_representation6_str_invalid)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_block_too_long)
    LT_CHECK_THROW(http::ip_representation("2001:db8:87214:3a90::12:4:4"));
LT_END_AUTO_TEST(ip_representation6_str_block_too_long)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_multiple_clusters)
    LT_CHECK_THROW(http::ip_representation("2001::3a90::12:4:4"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_multiple_clusters)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_too_long_before_nested)
    LT_CHECK_THROW(http::ip_representation("2001:db8:8714:3a90:13:12:13:192.0.2.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_too_long_before_nested)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_beyond255)
    LT_CHECK_THROW(http::ip_representation("::ffff:192.0.256.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_beyond255)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_more_than_4_parts)
    LT_CHECK_THROW(http::ip_representation("::ffff:192.0.5.128.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_more_than_4_parts)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_not_at_end)
    LT_CHECK_THROW(http::ip_representation("::ffff:192.0.256.128:ffff"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_not_at_end)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_starting_non_zero)
    LT_CHECK_THROW(http::ip_representation("0:0:1::ffff:192.0.5.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_starting_non_zero)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_starting_wrong_prefix)
    LT_CHECK_THROW(http::ip_representation("::ffcc:192.0.5.128"));
    LT_CHECK_THROW(http::ip_representation("::ccff:192.0.5.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_starting_wrong_prefix)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_sockaddr)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    http::ip_representation test_ip((sockaddr*) &ip4addr);
    
    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV4);

    for (int i = 0; i < 12; i++) {
        LT_CHECK_EQ(test_ip.pieces[i], 0);
    }

    LT_CHECK_EQ(test_ip.pieces[12], 127);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 0);
    LT_CHECK_EQ(test_ip.pieces[15], 1);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);   
LT_END_AUTO_TEST(ip_representation4_sockaddr)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_sockaddr)
    struct sockaddr_in6 ip6addr;

    ip6addr.sin6_family = AF_INET6;
    ip6addr.sin6_port = htons(3490);
    inet_pton(AF_INET6, "2001:db8:8714:3a90::12", &(ip6addr.sin6_addr));

    http::ip_representation test_ip((sockaddr*) &ip6addr);
    
    LT_CHECK_EQ(test_ip.ip_version, http::http_utils::IPV6);

    LT_CHECK_EQ(test_ip.pieces[0], 32);
    LT_CHECK_EQ(test_ip.pieces[1], 1);
    LT_CHECK_EQ(test_ip.pieces[2], 13);
    LT_CHECK_EQ(test_ip.pieces[3], 184);
    LT_CHECK_EQ(test_ip.pieces[4], 135);
    LT_CHECK_EQ(test_ip.pieces[5], 20);
    LT_CHECK_EQ(test_ip.pieces[6], 58);
    LT_CHECK_EQ(test_ip.pieces[7], 144);
    LT_CHECK_EQ(test_ip.pieces[8], 0);
    LT_CHECK_EQ(test_ip.pieces[9], 0);
    LT_CHECK_EQ(test_ip.pieces[10], 0);
    LT_CHECK_EQ(test_ip.pieces[11], 0);
    LT_CHECK_EQ(test_ip.pieces[12], 0);
    LT_CHECK_EQ(test_ip.pieces[13], 0);
    LT_CHECK_EQ(test_ip.pieces[14], 0);
    LT_CHECK_EQ(test_ip.pieces[15], 18);

    LT_CHECK_EQ(test_ip.mask, 0xFFFF);
LT_END_AUTO_TEST(ip_representation6_sockaddr)

LT_BEGIN_AUTO_TEST(http_utils_suite, load_file)
    LT_CHECK_EQ(std::string(http::load_file("test_content")), "test content of file");
LT_END_AUTO_TEST(load_file)

LT_BEGIN_AUTO_TEST(http_utils_suite, load_file_invalid)
    LT_CHECK_THROW(http::load_file("test_content_invalid"));
LT_END_AUTO_TEST(load_file_invalid)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation_less_than)
    LT_CHECK_EQ(http::ip_representation("127.0.0.1") < http::ip_representation("127.0.0.2"), true);
    LT_CHECK_EQ(http::ip_representation("128.0.0.1") < http::ip_representation("127.0.0.2"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.0.2") < http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.0.1") < http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.0.1") < http::ip_representation("127.0.0.1"), false);

    LT_CHECK_EQ(http::ip_representation("2001:db8::ff00:42:8329") < http::ip_representation("2001:db8::ff00:42:8329"), false);
    LT_CHECK_EQ(http::ip_representation("2001:db8::ff00:42:8330") < http::ip_representation("2001:db8::ff00:42:8329"), false);
    LT_CHECK_EQ(http::ip_representation("2001:db8::ff00:42:8329") < http::ip_representation("2001:db8::ff00:42:8330"), true);
    LT_CHECK_EQ(http::ip_representation("2002:db8::ff00:42:8329") < http::ip_representation("2001:db8::ff00:42:8330"), false);

    LT_CHECK_EQ(http::ip_representation("::192.0.2.128") < http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(http::ip_representation("::192.0.2.129") < http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(http::ip_representation("::192.0.2.128") < http::ip_representation("::192.0.2.129"), true);

    LT_CHECK_EQ(http::ip_representation("::ffff:192.0.2.128") < http::ip_representation("::ffff:192.0.2.128"), false);
    LT_CHECK_EQ(http::ip_representation("::ffff:192.0.2.129") < http::ip_representation("::ffff:192.0.2.128"), false);
    LT_CHECK_EQ(http::ip_representation("::ffff:192.0.2.128") < http::ip_representation("::ffff:192.0.2.129"), true);

    LT_CHECK_EQ(http::ip_representation("::ffff:192.0.2.128") < http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(http::ip_representation("::ffff:192.0.2.128") < http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(http::ip_representation("::192.0.2.128") < http::ip_representation("::ffff:192.0.2.129"), true);
LT_END_AUTO_TEST(ip_representation_less_than)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation_less_than_with_masks)
    LT_CHECK_EQ(http::ip_representation("127.0.*.*") < http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.0.1") < http::ip_representation("127.0.*.*"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.0.*") < http::ip_representation("127.0.*.*"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.*.1") < http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.0.1") < http::ip_representation("127.0.*.1"), false);
    LT_CHECK_EQ(http::ip_representation("127.1.0.1") < http::ip_representation("127.0.*.1"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.*.1") < http::ip_representation("127.1.0.1"), true);
    LT_CHECK_EQ(http::ip_representation("127.1.*.1") < http::ip_representation("127.0.*.1"), false);
    LT_CHECK_EQ(http::ip_representation("127.0.*.1") < http::ip_representation("127.1.*.1"), true);

    LT_CHECK_EQ(http::ip_representation("2001:db8::ff00:42:*") < http::ip_representation("2001:db8::ff00:42:8329"), false);
    LT_CHECK_EQ(http::ip_representation("2001:db8::ff00:42:8329") < http::ip_representation("2001:db8::ff00:42:*"), false);
LT_END_AUTO_TEST(ip_representation_less_than_with_masks)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
