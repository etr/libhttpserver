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
#include <string>
#include <vector>

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

// Test string_split with empty input
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_empty_input)
    string value = "";
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', true);
    LT_CHECK_EQ(actual.size(), 0);
LT_END_AUTO_TEST(split_string_empty_input)

// Test string_split with empty input and no collapse
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_empty_input_no_collapse)
    string value = "";
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', false);
    LT_CHECK_EQ(actual.size(), 0);
LT_END_AUTO_TEST(split_string_empty_input_no_collapse)

// Test string_split with only separators and collapse
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_only_separators_collapse)
    string value = "   ";  // Only spaces
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', true);
    LT_CHECK_EQ(actual.size(), 0);
LT_END_AUTO_TEST(split_string_only_separators_collapse)

// Test string_split with only separators and no collapse
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_only_separators_no_collapse)
    string value = "   ";  // Only spaces
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', false);
    // Should have 3 empty strings (between the 3 spaces) but last gets trimmed
    LT_CHECK_EQ(actual.size(), 3);
    LT_CHECK_EQ(actual[0], "");
    LT_CHECK_EQ(actual[1], "");
    LT_CHECK_EQ(actual[2], "");
LT_END_AUTO_TEST(split_string_only_separators_no_collapse)

// Test string_split with leading separator and no collapse
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_leading_separator_no_collapse)
    string value = " a b";  // Leading space
    string expected_arr[] = { "", "a", "b" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', false);

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(split_string_leading_separator_no_collapse)

// Test string_split with leading separator and collapse
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_leading_separator_collapse)
    string value = " a b";  // Leading space
    string expected_arr[] = { "a", "b" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', true);

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(split_string_leading_separator_collapse)

// Test to_upper_copy with empty string
LT_BEGIN_AUTO_TEST(string_utilities_suite, to_upper_copy_empty)
    LT_CHECK_EQ(httpserver::string_utilities::to_upper_copy(""), string(""));
LT_END_AUTO_TEST(to_upper_copy_empty)

// Test to_lower_copy with empty string
LT_BEGIN_AUTO_TEST(string_utilities_suite, to_lower_copy_empty)
    LT_CHECK_EQ(httpserver::string_utilities::to_lower_copy(""), string(""));
LT_END_AUTO_TEST(to_lower_copy_empty)

// Test to_upper_copy with already uppercase
LT_BEGIN_AUTO_TEST(string_utilities_suite, to_upper_copy_already_upper)
    LT_CHECK_EQ(httpserver::string_utilities::to_upper_copy("HELLO WORLD"), string("HELLO WORLD"));
LT_END_AUTO_TEST(to_upper_copy_already_upper)

// Test to_lower_copy with already lowercase
LT_BEGIN_AUTO_TEST(string_utilities_suite, to_lower_copy_already_lower)
    LT_CHECK_EQ(httpserver::string_utilities::to_lower_copy("hello world"), string("hello world"));
LT_END_AUTO_TEST(to_lower_copy_already_lower)

// Test string_split with different separator
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_comma_separator)
    string value = "a,b,c,d";
    string expected_arr[] = { "a", "b", "c", "d" };
    vector<string> expected(expected_arr, expected_arr + sizeof(expected_arr) / sizeof(expected_arr[0]));
    vector<string> actual = httpserver::string_utilities::string_split(value, ',', false);

    LT_CHECK_COLLECTIONS_EQ(expected.begin(), expected.end(), actual.begin());
LT_END_AUTO_TEST(split_string_comma_separator)

// Test string_split with single element
LT_BEGIN_AUTO_TEST(string_utilities_suite, split_string_single_element)
    string value = "hello";
    vector<string> actual = httpserver::string_utilities::string_split(value, ' ', true);
    LT_CHECK_EQ(actual.size(), 1);
    LT_CHECK_EQ(actual[0], "hello");
LT_END_AUTO_TEST(split_string_single_element)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
