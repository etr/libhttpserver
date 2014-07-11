/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014 Sebastiano Merlino

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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
