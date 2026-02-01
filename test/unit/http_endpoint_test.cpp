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

#include <iostream>
#include <string>
#include <vector>

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

// Test that invalid regex pattern throws exception (covers lines 114-116)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_invalid_regex_pattern)
    // Using unbalanced parentheses which is invalid regex
    LT_CHECK_THROW(http_endpoint("/path/(unclosed", false, true, true));
LT_END_AUTO_TEST(http_endpoint_invalid_regex_pattern)

// Test operator< when family_url differs (covers line 145)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, comparator_family_difference)
    http_endpoint family_ep("/path/to/resource", true, true, true);
    http_endpoint non_family_ep("/path/to/resource", false, true, true);

    // Family URL should come before non-family in ordering
    LT_CHECK_EQ(family_ep < non_family_ep, true);
    LT_CHECK_EQ(non_family_ep < family_ep, false);
LT_END_AUTO_TEST(comparator_family_difference)

// Test operator< when both are family URLs (covers line 146)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, comparator_same_family)
    http_endpoint family_a("/aaa", true, true, true);
    http_endpoint family_b("/bbb", true, true, true);

    // Should compare by url_normalized when both are family URLs
    LT_CHECK_EQ(family_a < family_b, true);
    LT_CHECK_EQ(family_b < family_a, false);
LT_END_AUTO_TEST(comparator_same_family)

// Test match with family URL and shorter incoming URL (covers line 152)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_family_shorter_url)
    // Family URL with 3 pieces
    http_endpoint family_ep("/path/to/resource", true, true, true);

    // Incoming URL with fewer pieces (covers the || short-circuit)
    http_endpoint short_url("/path");

    // Should still match using regex_match directly
    LT_CHECK_EQ(family_ep.match(short_url), false);
LT_END_AUTO_TEST(http_endpoint_match_family_shorter_url)

// Test match with non-family URL (covers line 153 directly)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_match_non_family)
    http_endpoint non_family_ep("/path/to/resource", false, true, true);
    http_endpoint incoming("/path/to/resource");

    // Non-family should use direct regex_match
    LT_CHECK_EQ(non_family_ep.match(incoming), true);
LT_END_AUTO_TEST(http_endpoint_match_non_family)

// Test URL parameter at first position (covers line 84 false branch, line 101 first==true)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_arg_first_position)
    http_endpoint test_endpoint("/{arg}/rest/of/path", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/{arg}/rest/of/path");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/([^\\/]+)/rest/of/path$");

    string expected_pars_arr[] = { "arg" };
    vector<string> expected_pars(expected_pars_arr, expected_pars_arr + 1);
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pars().begin(),
                            test_endpoint.get_url_pars().end(),
                            expected_pars.begin());

    int expected_chunk_positions_arr[] = { 0 };
    vector<int> expected_chunk_positions(expected_chunk_positions_arr, expected_chunk_positions_arr + 1);
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_chunk_positions().begin(),
                            test_endpoint.get_chunk_positions().end(),
                            expected_chunk_positions.begin());

    // Verify it matches correctly
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/value/rest/of/path")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/wrong/path")), false);
LT_END_AUTO_TEST(http_endpoint_arg_first_position)

// Test custom regex pattern at first position (covers line 85 starting with ^)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_custom_regex_first)
    // Note: Custom regex starting with ^ at first position
    http_endpoint test_endpoint("/{id|([0-9]+)}/data", false, true, true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/{id|([0-9]+)}/data");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/([0-9]+)/data$");

    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/123/data")), true);
    LT_CHECK_EQ(test_endpoint.match(http_endpoint("/abc/data")), false);
LT_END_AUTO_TEST(http_endpoint_custom_regex_first)

// Test URL pattern where first part starts with ^ (caret)
// Covers http_endpoint.cpp line 85 (parts[i][0] == '^' branch)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_caret_at_start)
    // When first part[0] == '^', the prefix should be cleared
    // The regex pattern starting with ^ at the first position
    http_endpoint test_endpoint("/^api", false, true, true);

    // The normalized URL should not have double caret (^^ would be wrong)
    LT_CHECK_EQ(test_endpoint.get_url_normalized().find("^^") == std::string::npos, true);
    // Should start with ^api (not ^/^api)
    LT_CHECK_EQ(test_endpoint.get_url_normalized().substr(0, 4), "^api");
LT_END_AUTO_TEST(http_endpoint_caret_at_start)

// Test URL with consecutive slashes creating empty parts
// Covers http_endpoint.cpp line 83 (parts[i] == "" condition)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_consecutive_slashes)
    // Consecutive slashes create empty parts which should be skipped in processing
    // but the original URL is preserved in url_complete
    http_endpoint test_endpoint("//path//to//resource", false, true, true);

    // URL is preserved with consecutive slashes (leading / is normalized)
    LT_CHECK_EQ(test_endpoint.get_url_complete(), "//path//to//resource");

    // But url_pieces should only contain non-empty parts
    std::vector<std::string> pieces = test_endpoint.get_url_pieces();
    LT_CHECK_EQ(pieces.size() > 0, true);  // At least some pieces
    for (const auto& piece : pieces) {
        // No empty pieces should be in the result
        LT_CHECK_EQ(piece.empty(), false);
    }
LT_END_AUTO_TEST(http_endpoint_consecutive_slashes)

// Test URL part that is just "^" by itself (edge case)
LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_caret_only_part)
    // Part that is just "^" - tests the empty string after ^ edge case
    http_endpoint test_endpoint("/api/^/data", false, true, true);

    // Should be handled correctly
    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/api/^/data");
LT_END_AUTO_TEST(http_endpoint_caret_only_part)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
