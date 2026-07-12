/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

#include <cstring>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include "./littletest.hpp"

// unescaper_func is the MHD_OPTION_UNESCAPE_CALLBACK registered as a
// static member of webserver_impl in src/detail/webserver_callbacks.cpp
// (see the historical note there, TASK-073). It is a deliberate no-op:
// it must not mutate the buffer it is handed (decoding is performed
// later, by libhttpserver itself) and must return the string length so
// MHD does not truncate downstream key/value storage. The only existing
// coverage was indirect, via the custom_unescaper integration test; this
// unit test pins the static callback directly, the same
// HTTPSERVER_COMPILATION-gated pattern already used by uri_log_test.cpp
// and post_iterator_null_key_test.cpp.

LT_BEGIN_SUITE(unescaper_func_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(unescaper_func_suite)

LT_BEGIN_AUTO_TEST(unescaper_func_suite, leaves_buffer_unchanged_and_returns_length)
    char buf[] = "a%2Fb%20c";
    const size_t expected_len = std::strlen(buf);
    size_t result = httpserver::detail::webserver_impl::unescaper_func(
        nullptr, nullptr, buf);
    LT_CHECK_EQ(result, expected_len);
    LT_CHECK_EQ(std::string(buf), std::string("a%2Fb%20c"));
LT_END_AUTO_TEST(leaves_buffer_unchanged_and_returns_length)

LT_BEGIN_AUTO_TEST(unescaper_func_suite, null_string_returns_zero)
    size_t result = httpserver::detail::webserver_impl::unescaper_func(
        nullptr, nullptr, nullptr);
    LT_CHECK_EQ(result, static_cast<size_t>(0));
LT_END_AUTO_TEST(null_string_returns_zero)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
