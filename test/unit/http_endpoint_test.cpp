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

#include "httpserver/details/http_endpoint.hpp"

#include "./littletest.hpp"

using httpserver::details::http_endpoint;
using std::string;
using std::vector;

LT_BEGIN_SUITE(http_endpoint_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_endpoint_suite)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_default)
    http_endpoint test_endpoint;

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "/");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);
    LT_CHECK_EQ(test_endpoint.get_url_pieces().size(), 0);
    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), false);
LT_END_AUTO_TEST(http_endpoint_default)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_registration)
    http_endpoint test_endpoint("/path/to/resource", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource$");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_from_string_registration)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_not_beginning_with_slash)
    http_endpoint test_endpoint("path/to/resource", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource$");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_from_string_not_beginning_with_slash)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_ending_with_slash)
    http_endpoint test_endpoint("path/to/resource/", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource$");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_from_string_ending_with_slash)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_family)
    http_endpoint test_endpoint("/path/to/resource", true, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource$");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), true);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_from_string_family)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_default_no_regex)
    http_endpoint test_endpoint("/path/to/resource");

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "/path/to/resource");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), false);
LT_END_AUTO_TEST(http_endpoint_default_no_regex)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_no_regex)
    http_endpoint test_endpoint("/path/to/resource", false, false, false);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "/path/to/resource");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), false);
LT_END_AUTO_TEST(http_endpoint_from_string_no_regex)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_registration)
    http_endpoint test_endpoint("/path/to/resource", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource$");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_registration)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_registration_nested_regex)
    http_endpoint test_endpoint("/path/to/resource/with/[0-9]+/to/fetch", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource/with/[0-9]+/to/fetch");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource/with/[0-9]+/to/fetch$");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource", "with", "[0-9]+", "to", "fetch" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_registration_nested_regex)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_registration_arg)
    http_endpoint test_endpoint("/path/to/resource/with/{arg}/to/fetch", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource/with/{arg}/to/fetch");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource/with/([^\\/]+)/to/fetch$");

    string expected_pars_arr[] = { "arg" };
    vector<string> expected_pars(expected_pars_arr, expected_pars_arr + sizeof(expected_pars_arr) / sizeof(expected_pars_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pars().begin(), test_endpoint.get_url_pars().end(), expected_pars.begin());

    string expected_arr[] = { "path", "to", "resource", "with", "{arg}", "to", "fetch" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    int expected_chunk_positions_arr[] = { 4 };
    vector<int> expected_chunk_positions(expected_chunk_positions_arr, expected_chunk_positions_arr + sizeof(expected_chunk_positions_arr) / sizeof(expected_chunk_positions_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_chunk_positions().begin(), test_endpoint.get_chunk_positions().end(), expected_chunk_positions.begin());

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_registration_arg)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_registration_arg_custom_regex)
    http_endpoint test_endpoint("/path/to/resource/with/{arg|([0-9]+)}/to/fetch", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource/with/{arg|([0-9]+)}/to/fetch");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource/with/([0-9]+)/to/fetch$");

    string expected_pars_arr[] = { "arg" };
    vector<string> expected_pars(expected_pars_arr, expected_pars_arr + sizeof(expected_pars_arr) / sizeof(expected_pars_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pars().begin(), test_endpoint.get_url_pars().end(), expected_pars.begin());

    string expected_arr[] = { "path", "to", "resource", "with", "{arg|([0-9]+)}", "to", "fetch" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    int expected_chunk_positions_arr[] = { 4 };
    vector<int> expected_chunk_positions(expected_chunk_positions_arr, expected_chunk_positions_arr + sizeof(expected_chunk_positions_arr) / sizeof(expected_chunk_positions_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_chunk_positions().begin(), test_endpoint.get_chunk_positions().end(), expected_chunk_positions.begin());

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_registration_arg_custom_regex)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_registration_invalid_arg)
    LT_CHECK_THROW(http_endpoint("/path/to/resource/with/{}/to/fetch", false, true));
LT_END_AUTO_TEST(http_endpoint_registration_invalid_arg)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_copy_constructor)
    http_endpoint a("/path/to/resource/with/{arg|([0-9]+)}/to/fetch", false, true, true);
    http_endpoint b(a);

    LT_CHECK_EQ(a.get_url_complete(), b.get_url_complete());
    LT_CHECK_EQ(a.get_url_normalized(), b.get_url_normalized());
    LT_CHECK_COLLECTIONS_EQ(a.get_url_pars().begin(), a.get_url_pars().end(), b.get_url_pars().begin());
    LT_CHECK_COLLECTIONS_EQ(a.get_url_pieces().begin(), a.get_url_pieces().end(), b.get_url_pieces().begin());
    LT_CHECK_COLLECTIONS_EQ(a.get_chunk_positions().begin(), a.get_chunk_positions().end(), b.get_chunk_positions().begin());
    LT_CHECK_EQ(a.is_family_url(), b.is_family_url());
    LT_CHECK_EQ(a.is_regex_compiled(), b.is_regex_compiled());

    LT_CHECK_EQ(a.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
    LT_CHECK_EQ(b.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
LT_END_AUTO_TEST(http_endpoint_copy_constructor)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_assignment)
    http_endpoint a("/path/to/resource/with/{arg|([0-9]+)}/to/fetch", false, true, true);
    http_endpoint b("whatever/initial/value");

    LT_CHECK_NEQ(a.get_url_complete(), b.get_url_complete());

    std::cout << "before assigning" << std::endl;
    b = a;
    std::cout << "after assigning" << std::endl;

    LT_CHECK_EQ(a.get_url_complete(), b.get_url_complete());
    LT_CHECK_EQ(a.get_url_normalized(), b.get_url_normalized());
    LT_CHECK_COLLECTIONS_EQ(a.get_url_pars().begin(), a.get_url_pars().end(), b.get_url_pars().begin());
    LT_CHECK_COLLECTIONS_EQ(a.get_url_pieces().begin(), a.get_url_pieces().end(), b.get_url_pieces().begin());
    LT_CHECK_COLLECTIONS_EQ(a.get_chunk_positions().begin(), a.get_chunk_positions().end(), b.get_chunk_positions().begin());
    LT_CHECK_EQ(a.is_family_url(), b.is_family_url());
    LT_CHECK_EQ(a.is_regex_compiled(), b.is_regex_compiled());

    LT_CHECK_EQ(a.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
    LT_CHECK_EQ(b.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
LT_END_AUTO_TEST(http_endpoint_assignment)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_regex)
    http_endpoint test_endpoint("/path/to/resource/", false, true, true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to2/resource")), false);
LT_END_AUTO_TEST(http_endpoint_match_regex)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_regex_nested)
    http_endpoint test_endpoint("/path/to/resource/with/[0-9]+/to/fetch", false, true, true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/0/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("path/to/resource/with/1/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/1/to/fetch/")), true);
LT_END_AUTO_TEST(http_endpoint_match_regex_nested)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_regex_nested_capture)
    http_endpoint test_endpoint("/path/to/resource/with/([0-9]+)/to/fetch", false, true, true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/0/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("path/to/resource/with/1/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/1/to/fetch/")), true);
LT_END_AUTO_TEST(http_endpoint_match_regex_nested_capture)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_regex_nested_arg)
    http_endpoint test_endpoint("/path/to/resource/with/{arg}/to/fetch", false, true, true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/0/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("path/to/resource/with/1/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/1/to/fetch/")), true);
LT_END_AUTO_TEST(http_endpoint_match_regex_nested_arg)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_regex_nested_custom_arg)
    http_endpoint test_endpoint("/path/to/resource/with/{arg|([0-9]+)}/to/fetch", false, true, true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/0/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/10/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("path/to/resource/with/1/to/fetch")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/with/1/to/fetch/")), true);
LT_END_AUTO_TEST(http_endpoint_match_regex_nested_custom_arg)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_regex_family)
    http_endpoint test_endpoint("/path/to/resource", true, true, true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("path/to/resource")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("path/to/resource/")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/path/to/resource/followed/by/anything")), true);

    LT_CHECK_EQ(test_endpoint.match(http_endpoint("path/to2/resource")), false);
LT_END_AUTO_TEST(http_endpoint_match_regex_family)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_regex_disabled)
    http_endpoint test_endpoint("/path/to/resource", false, true, false);
    LT_CHECK_THROW(test_endpoint.match(http_endpoint("/path/to/resource")));
LT_END_AUTO_TEST(http_endpoint_match_regex_disabled)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_cannot_use_regex_if_not_registering)
    LT_CHECK_THROW(http_endpoint("/path/to/resource", false, false, true));
LT_END_AUTO_TEST(http_endpoint_cannot_use_regex_if_not_registering)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, comparator)
    LT_CHECK_EQ(http_endpoint("/a/b") < http_endpoint("/a/c"), true);
    LT_CHECK_EQ(http_endpoint("/a/c") < http_endpoint("/a/b"), false);

    LT_CHECK_EQ(http_endpoint("/a/b") < http_endpoint("/a/b/c"), true);
    LT_CHECK_EQ(http_endpoint("/a/b/c") < http_endpoint("/a/b"), false);
LT_END_AUTO_TEST(comparator)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
