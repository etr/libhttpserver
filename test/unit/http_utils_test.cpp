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

#include "httpserver/http_utils.hpp"

#if defined(_WIN32) && !defined(__CYGWIN__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <sys/stat.h>
#include <cstdio>

#include "./littletest.hpp"

using std::string;
using std::vector;

LT_BEGIN_SUITE(http_utils_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_utils_suite)

LT_BEGIN_AUTO_TEST(http_utils_suite, unescape)
    int with_plus_buff_len = 6;
    char* with_plus = new char[with_plus_buff_len];
    snprintf(with_plus, with_plus_buff_len, "%s", "A%20B");
    std::string string_with_plus = with_plus;
    int expected_size = httpserver::http::http_unescape(&string_with_plus);

    int expected_buff_len = 4;
    char* expected = new char[expected_buff_len];
    snprintf(expected, expected_buff_len, "%s", "A B");

    std::cout << "|||||" << string_with_plus << "||||" << std::endl;
    std::cout << expected << std::endl;

    LT_CHECK_EQ(string_with_plus, string(expected));
    LT_CHECK_EQ(expected_size, 3);

    delete[] with_plus;
    delete[] expected;
LT_END_AUTO_TEST(unescape)

LT_BEGIN_AUTO_TEST(http_utils_suite, unescape_plus)
    int with_plus_buff_len = 4;
    char* with_plus = new char[with_plus_buff_len];
    snprintf(with_plus, with_plus_buff_len, "%s", "A+B");
    std::string string_with_plus = with_plus;
    int expected_size = httpserver::http::http_unescape(&string_with_plus);

    int expected_buff_len = 4;
    char* expected = new char[expected_buff_len];
    snprintf(expected, expected_buff_len, "%s", "A B");

    LT_CHECK_EQ(string_with_plus, string(expected));
    LT_CHECK_EQ(expected_size, 3);

    delete[] with_plus;
    delete[] expected;
LT_END_AUTO_TEST(unescape_plus)

LT_BEGIN_AUTO_TEST(http_utils_suite, unescape_partial_marker)
    int size = 6;
    char* with_marker = new char[size];
    snprintf(with_marker, size, "%s", "A+B%0");
    std::string string_with_marker = with_marker;
    int expected_size = httpserver::http::http_unescape(&string_with_marker);

    char* expected = new char[size];
    snprintf(expected, size, "%s", "A B%0");

    LT_CHECK_EQ(string_with_marker, string(expected));
    LT_CHECK_EQ(expected_size, 5);

    delete[] with_marker;
    delete[] expected;
LT_END_AUTO_TEST(unescape_partial_marker)

LT_BEGIN_AUTO_TEST(http_utils_suite, tokenize_url)
    string value = "test/this/url/here";
    string expected_arr[] = { "test", "this", "url", "here" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::http::http_utils::tokenize_url(value, '/');

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(tokenize_url)

LT_BEGIN_AUTO_TEST(http_utils_suite, tokenize_url_multiple_spaces)
    string value = "test//this//url//here";
    string expected_arr[] = { "test", "this", "url", "here" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::http::http_utils::tokenize_url(value, '/');

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(tokenize_url_multiple_spaces)

LT_BEGIN_AUTO_TEST(http_utils_suite, tokenize_url_end_slash)
    string value = "test/this/url/here/";
    string expected_arr[] = { "test", "this", "url", "here" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::http::http_utils::tokenize_url(value, '/');

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(tokenize_url_end_slash)

LT_BEGIN_AUTO_TEST(http_utils_suite, standardize_url)
    string url = "/", result;
    result = httpserver::http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/");

    url = "/abc/", result = "";
    result = httpserver::http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc");

    url = "/abc", result = "";
    result = httpserver::http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc");

    url = "/abc/pqr/", result = "";
    result = httpserver::http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc/pqr");

    url = "/abc/pqr", result = "";
    result = httpserver::http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc/pqr");

    url = "/abc//pqr", result = "";
    result = httpserver::http::http_utils::standardize_url(url);
    LT_CHECK_EQ(result, "/abc/pqr");
LT_END_AUTO_TEST(standardize_url)

LT_BEGIN_AUTO_TEST(http_utils_suite, generate_random_upload_filename)
    struct stat sb;
    string directory = ".", filename = "";
    string expected_output = directory + httpserver::http::http_utils::path_separator + httpserver::http::http_utils::upload_filename_template;
    try {
        filename = httpserver::http::http_utils::generate_random_upload_filename(directory);
    } catch(const httpserver::http::generateFilenameException& e) {
        LT_FAIL(e.what());
    }
    LT_CHECK_EQ(stat(filename.c_str(), &sb), 0);
    // unlink the file again, to not mess up the test directory with files
    unlink(filename.c_str());
    // omit the last 6 signs in the check, as the "XXXXXX" is substituted to be random and unique
    LT_CHECK_EQ(filename.substr(0, filename.size() - 6), expected_output.substr(0, expected_output.size() - 6));
LT_END_AUTO_TEST(generate_random_upload_filename)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    string result = httpserver::http::get_ip_str((struct sockaddr*) &ip4addr);
    unsigned short port = httpserver::http::get_port((struct sockaddr*) &ip4addr);

    LT_CHECK_EQ(result, "127.0.0.1");
    LT_CHECK_EQ(port, htons(3490));
LT_END_AUTO_TEST(ip_to_str)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str6)
    struct sockaddr_in6 ip6addr;

    ip6addr.sin6_family = AF_INET6;
    ip6addr.sin6_port = htons(3490);
    inet_pton(AF_INET6, "2001:db8:8714:3a90::12", &(ip6addr.sin6_addr));

    string result = httpserver::http::get_ip_str((struct sockaddr *) &ip6addr);
    unsigned short port = httpserver::http::get_port((struct sockaddr*) &ip6addr);

    LT_CHECK_EQ(result, "2001:db8:8714:3a90::12");
    LT_CHECK_EQ(port, htons(3490));
LT_END_AUTO_TEST(ip_to_str6)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str_invalid_family)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = 55;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    LT_CHECK_THROW(httpserver::http::get_ip_str((struct sockaddr*) &ip4addr));
LT_END_AUTO_TEST(ip_to_str_invalid_family)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_to_str_null)
    LT_CHECK_THROW(httpserver::http::get_ip_str((struct sockaddr*) nullptr));
LT_END_AUTO_TEST(ip_to_str_null)

LT_BEGIN_AUTO_TEST(http_utils_suite, get_port_invalid_family)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = 55;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    LT_CHECK_THROW(httpserver::http::get_port((struct sockaddr*) &ip4addr));
LT_END_AUTO_TEST(get_port_invalid_family)

LT_BEGIN_AUTO_TEST(http_utils_suite, get_port_null)
    LT_CHECK_THROW(httpserver::http::get_port((struct sockaddr*) nullptr));
LT_END_AUTO_TEST(get_port_null)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_str)
    httpserver::http::ip_representation test_ip("192.168.5.5");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV4);

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
    httpserver::http::ip_representation test_ip("192.168.*.*");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV4);

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
    LT_CHECK_THROW(httpserver::http::ip_representation("192.168.5.5.5"));
LT_END_AUTO_TEST(ip_representation4_str_invalid)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_str_beyond255)
    LT_CHECK_THROW(httpserver::http::ip_representation("192.168.256.5"));
LT_END_AUTO_TEST(ip_representation4_str_beyond255)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str)
    httpserver::http::ip_representation test_ip("2001:db8:8714:3a90::12");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    httpserver::http::ip_representation test_ip("2001:db8:8714:3a90:*:*");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    httpserver::http::ip_representation test_ip("::ffff:192.0.2.128");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    LT_CHECK_NOTHROW(httpserver::http::ip_representation("::192.0.2.128"));
    httpserver::http::ip_representation test_ip("::192.0.2.128");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    httpserver::http::ip_representation test_ip("::ffff:192.0.*.*");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    httpserver::http::ip_representation test_ip("2001:db8::ff00:42:8329");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    httpserver::http::ip_representation test_ip("::1");

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    LT_CHECK_EQ(httpserver::http::ip_representation("::1").weight(), 16);
    LT_CHECK_EQ(httpserver::http::ip_representation("192.168.0.1").weight(), 16);
    LT_CHECK_EQ(httpserver::http::ip_representation("192.168.*.*").weight(), 14);
    LT_CHECK_EQ(httpserver::http::ip_representation("::ffff:192.0.*.*").weight(), 14);
    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8:8714:3a90:*:*").weight(), 12);
    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8:8714:3a90:8714:2001:db8:3a90").weight(), 16);
    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8:8714:3a90:8714:2001:*:*").weight(), 12);
    LT_CHECK_EQ(httpserver::http::ip_representation("*:*:*:*:*:*:*:*").weight(), 0);
LT_END_AUTO_TEST(ip_representation_weight)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid)
    LT_CHECK_THROW(httpserver::http::ip_representation("2001:db8:8714:3a90::12:4:4:4"));
LT_END_AUTO_TEST(ip_representation6_str_invalid)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_block_too_long)
    LT_CHECK_THROW(httpserver::http::ip_representation("2001:db8:87214:3a90::12:4:4"));
LT_END_AUTO_TEST(ip_representation6_str_block_too_long)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_multiple_clusters)
    LT_CHECK_THROW(httpserver::http::ip_representation("2001::3a90::12:4:4"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_multiple_clusters)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_too_long_before_nested)
    LT_CHECK_THROW(httpserver::http::ip_representation("2001:db8:8714:3a90:13:12:13:192.0.2.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_too_long_before_nested)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_beyond255)
    LT_CHECK_THROW(httpserver::http::ip_representation("::ffff:192.0.256.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_beyond255)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_more_than_4_parts)
    LT_CHECK_THROW(httpserver::http::ip_representation("::ffff:192.0.5.128.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_more_than_4_parts)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_not_at_end)
    LT_CHECK_THROW(httpserver::http::ip_representation("::ffff:192.0.256.128:ffff"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_not_at_end)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_starting_non_zero)
    LT_CHECK_THROW(httpserver::http::ip_representation("0:0:1::ffff:192.0.5.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_starting_non_zero)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation6_str_invalid_nested_starting_wrong_prefix)
    LT_CHECK_THROW(httpserver::http::ip_representation("::ffcc:192.0.5.128"));
    LT_CHECK_THROW(httpserver::http::ip_representation("::ccff:192.0.5.128"));
LT_END_AUTO_TEST(ip_representation6_str_invalid_nested_starting_wrong_prefix)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation4_sockaddr)
    struct sockaddr_in ip4addr;

    ip4addr.sin_family = AF_INET;
    ip4addr.sin_port = htons(3490);
    ip4addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    httpserver::http::ip_representation test_ip(reinterpret_cast<sockaddr*>(&ip4addr));

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV4);

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

    httpserver::http::ip_representation test_ip(reinterpret_cast<sockaddr*>(&ip6addr));

    LT_CHECK_EQ(test_ip.ip_version, httpserver::http::http_utils::IPV6);

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
    LT_CHECK_EQ(httpserver::http::load_file("test_content"), "test content of file\n");
LT_END_AUTO_TEST(load_file)

LT_BEGIN_AUTO_TEST(http_utils_suite, load_file_invalid)
    LT_CHECK_THROW(httpserver::http::load_file("test_content_invalid"));
LT_END_AUTO_TEST(load_file_invalid)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation_less_than)
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.0.1") < httpserver::http::ip_representation("127.0.0.2"), true);
    LT_CHECK_EQ(httpserver::http::ip_representation("128.0.0.1") < httpserver::http::ip_representation("127.0.0.2"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.0.2") < httpserver::http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.0.1") < httpserver::http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.0.1") < httpserver::http::ip_representation("127.0.0.1"), false);

    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8::ff00:42:8329") < httpserver::http::ip_representation("2001:db8::ff00:42:8329"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8::ff00:42:8330") < httpserver::http::ip_representation("2001:db8::ff00:42:8329"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8::ff00:42:8329") < httpserver::http::ip_representation("2001:db8::ff00:42:8330"), true);
    LT_CHECK_EQ(httpserver::http::ip_representation("2002:db8::ff00:42:8329") < httpserver::http::ip_representation("2001:db8::ff00:42:8330"), false);

    LT_CHECK_EQ(httpserver::http::ip_representation("::192.0.2.128") < httpserver::http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("::192.0.2.129") < httpserver::http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("::192.0.2.128") < httpserver::http::ip_representation("::192.0.2.129"), true);

    LT_CHECK_EQ(httpserver::http::ip_representation("::ffff:192.0.2.128") < httpserver::http::ip_representation("::ffff:192.0.2.128"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("::ffff:192.0.2.129") < httpserver::http::ip_representation("::ffff:192.0.2.128"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("::ffff:192.0.2.128") < httpserver::http::ip_representation("::ffff:192.0.2.129"), true);

    LT_CHECK_EQ(httpserver::http::ip_representation("::ffff:192.0.2.128") < httpserver::http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("::ffff:192.0.2.128") < httpserver::http::ip_representation("::192.0.2.128"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("::192.0.2.128") < httpserver::http::ip_representation("::ffff:192.0.2.129"), true);
LT_END_AUTO_TEST(ip_representation_less_than)

LT_BEGIN_AUTO_TEST(http_utils_suite, ip_representation_less_than_with_masks)
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.*.*") < httpserver::http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.0.1") < httpserver::http::ip_representation("127.0.*.*"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.0.*") < httpserver::http::ip_representation("127.0.*.*"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.*.1") < httpserver::http::ip_representation("127.0.0.1"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.0.1") < httpserver::http::ip_representation("127.0.*.1"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.1.0.1") < httpserver::http::ip_representation("127.0.*.1"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.*.1") < httpserver::http::ip_representation("127.1.0.1"), true);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.1.*.1") < httpserver::http::ip_representation("127.0.*.1"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("127.0.*.1") < httpserver::http::ip_representation("127.1.*.1"), true);

    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8::ff00:42:*") < httpserver::http::ip_representation("2001:db8::ff00:42:8329"), false);
    LT_CHECK_EQ(httpserver::http::ip_representation("2001:db8::ff00:42:8329") < httpserver::http::ip_representation("2001:db8::ff00:42:*"), false);
LT_END_AUTO_TEST(ip_representation_less_than_with_masks)

LT_BEGIN_AUTO_TEST(http_utils_suite, dump_header_map)
    std::map<std::string_view, std::string_view, httpserver::http::header_comparator> header_map;
    header_map["HEADER_ONE"] = "VALUE_ONE";
    header_map["HEADER_TWO"] = "VALUE_TWO";
    header_map["HEADER_THREE"] = "VALUE_THREE";

    std::stringstream ss;
    httpserver::http::dump_header_map(ss, "prefix", header_map);
    LT_CHECK_EQ(ss.str(), "    prefix [HEADER_ONE:\"VALUE_ONE\" HEADER_TWO:\"VALUE_TWO\" HEADER_THREE:\"VALUE_THREE\" ]\n");
LT_END_AUTO_TEST(dump_header_map)

LT_BEGIN_AUTO_TEST(http_utils_suite, dump_header_map_no_prefix)
    std::map<std::string_view, std::string_view, httpserver::http::header_comparator> header_map;
    header_map["HEADER_ONE"] = "VALUE_ONE";
    header_map["HEADER_TWO"] = "VALUE_TWO";
    header_map["HEADER_THREE"] = "VALUE_THREE";

    std::stringstream ss;
    httpserver::http::dump_header_map(ss, "", header_map);
    LT_CHECK_EQ(ss.str(), "     [HEADER_ONE:\"VALUE_ONE\" HEADER_TWO:\"VALUE_TWO\" HEADER_THREE:\"VALUE_THREE\" ]\n");
LT_END_AUTO_TEST(dump_header_map_no_prefix)

LT_BEGIN_AUTO_TEST(http_utils_suite, dump_arg_map)
    httpserver::http::arg_view_map arg_map;
    arg_map["ARG_ONE"].values.push_back("VALUE_ONE");
    arg_map["ARG_TWO"].values.push_back("VALUE_TWO");
    arg_map["ARG_THREE"].values.push_back("VALUE_THREE");

    std::stringstream ss;
    httpserver::http::dump_arg_map(ss, "prefix", arg_map);
    LT_CHECK_EQ(ss.str(), "    prefix [ARG_ONE:[\"VALUE_ONE\"] ARG_TWO:[\"VALUE_TWO\"] ARG_THREE:[\"VALUE_THREE\"] ]\n");
LT_END_AUTO_TEST(dump_arg_map)

LT_BEGIN_AUTO_TEST(http_utils_suite, dump_arg_map_no_prefix)
    httpserver::http::arg_view_map arg_map;
    arg_map["ARG_ONE"].values.push_back("VALUE_ONE");
    arg_map["ARG_TWO"].values.push_back("VALUE_TWO");
    arg_map["ARG_THREE"].values.push_back("VALUE_THREE");

    std::stringstream ss;
    httpserver::http::dump_arg_map(ss, "", arg_map);
    LT_CHECK_EQ(ss.str(), "     [ARG_ONE:[\"VALUE_ONE\"] ARG_TWO:[\"VALUE_TWO\"] ARG_THREE:[\"VALUE_THREE\"] ]\n");
LT_END_AUTO_TEST(dump_arg_map_no_prefix)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
