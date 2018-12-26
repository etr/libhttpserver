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

#include "littletest.hpp"
#include "details/http_endpoint.hpp"

using namespace httpserver;
using namespace std;
using namespace details;

LT_BEGIN_SUITE(http_endpoint_suite)
    void set_up()
    {
    }

    void tear_down()
    {
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

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_default_family)
    http_endpoint test_endpoint(true);

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "/");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);
    LT_CHECK_EQ(test_endpoint.get_url_pieces().size(), 0);
    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), true);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), false);
LT_END_AUTO_TEST(http_endpoint_default_family)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_default)
    http_endpoint test_endpoint(std::string("/path/to/resource"));

    LT_CHECK_EQ(test_endpoint.get_url_complete(), "/path/to/resource");
    LT_CHECK_EQ(test_endpoint.get_url_normalized(), "^/path/to/resource$");

    LT_CHECK_EQ(test_endpoint.get_url_pars().size(), 0);

    string expected_arr[] = { "path", "to", "resource" };
    vector<string> expected_pieces(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    LT_CHECK_COLLECTIONS_EQ(test_endpoint.get_url_pieces().begin(), test_endpoint.get_url_pieces().end(), expected_pieces.begin());

    LT_CHECK_EQ(test_endpoint.get_chunk_positions().size(), 0);

    LT_CHECK_EQ(test_endpoint.is_family_url(), false);
    LT_CHECK_EQ(test_endpoint.is_regex_compiled(), true);
LT_END_AUTO_TEST(http_endpoint_from_string_default)

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_not_beginning_with_slash)
    http_endpoint test_endpoint(std::string("path/to/resource"));

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

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_family)
    http_endpoint test_endpoint(std::string("/path/to/resource"), true);

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

LT_BEGIN_AUTO_TEST(http_endpoint_suite, http_endpoint_from_string_no_regex)
    http_endpoint test_endpoint(std::string("/path/to/resource"), false, false, false);

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
    http_endpoint test_endpoint(std::string("/path/to/resource"), false, true);

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
    http_endpoint test_endpoint(std::string("/path/to/resource/with/[0-9]+/to/fetch"), false, true);

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
    http_endpoint test_endpoint(std::string("/path/to/resource/with/{arg}/to/fetch"), false, true);

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
    http_endpoint test_endpoint(std::string("/path/to/resource/with/{arg|([0-9]+)}/to/fetch"), false, true);

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
    LT_CHECK_THROW(http_endpoint(std::string("/path/to/resource/with/{}/to/fetch"), false, true));
LT_END_AUTO_TEST(http_endpoint_registration_invalid_arg)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
