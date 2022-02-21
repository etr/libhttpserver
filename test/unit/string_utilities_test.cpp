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

#include "httpserver/string_utilities.hpp"

#include <cstdio>

#include "./littletest.hpp"

using std::string;
using std::vector;

LT_BEGIN_SUITE(string_utilities_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(string_utilities_suite)

LT_BEGIN_AUTO_TEST(string_utilities_suite, to_upper_copy)
    LT_CHECK_EQ(httpserver::string_utilities::to_upper_copy("test message"), string("TEST MESSAGE"));
    LT_CHECK_EQ(httpserver::string_utilities::to_upper_copy("tEsT mEssAge 245&$"), string("TEST MESSAGE 245&$"));
LT_END_AUTO_TEST(to_upper_copy)

LT_BEGIN_AUTO_TEST(string_utilities_suite, to_lower_copy)
    LT_CHECK_EQ(httpserver::string_utilities::to_lower_copy("TEST MESSAGE"), string("test message"));
    LT_CHECK_EQ(httpserver::string_utilities::to_lower_copy("tEsT mEssAge 245&$"), string("test message 245&$"));
LT_END_AUTO_TEST(to_lower_copy)

LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string)
    string value = "test this message here";
    string expected_arr[] = { "test", "this", "message", "here" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', false);

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(split_string)

LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_multiple_spaces)
    string value = "test  this  message  here";
    string expected_arr[] = { "test", "", "this", "", "message", "", "here" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', false);

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(split_string_multiple_spaces)

LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_multiple_spaces_collapse)
    string value = "test  this  message  here";
    string expected_arr[] = { "test", "this", "message", "here" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', true);

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(split_string_multiple_spaces_collapse)

LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_end_space)
    string value = "test this message here ";
    string expected_arr[] = { "test", "this", "message", "here" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', false);

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(split_string_end_space)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
