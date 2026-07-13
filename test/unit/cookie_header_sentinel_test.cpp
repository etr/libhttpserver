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

// TASK-064 Cycle 1 sentinel.
//
// Pins that the new public header <httpserver/cookie.hpp> exists, is
// reachable through the umbrella <httpserver.hpp>, declares the
// httpserver::cookie value type and the same_site_mode enum, and that
// cookie is copy + move constructible (required for std::vector<cookie>
// and for the with_cookie(cookie) by-value overload).
//
// Pure compile/runtime shape test -- empty LDADD.

#include <type_traits>
#include <cstdint>
#include <string>

#include "./httpserver.hpp"      // public umbrella -- must transitively pull in cookie.hpp
#include "./littletest.hpp"

using httpserver::cookie;
using httpserver::same_site_mode;

LT_BEGIN_SUITE(cookie_header_sentinel_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(cookie_header_sentinel_suite)

LT_BEGIN_AUTO_TEST(cookie_header_sentinel_suite, cookie_is_class_type)
    static_assert(std::is_class_v<cookie>,
                  "httpserver::cookie must be a class type");
LT_END_AUTO_TEST(cookie_is_class_type)

LT_BEGIN_AUTO_TEST(cookie_header_sentinel_suite, cookie_default_constructible_empty)
    cookie c;
    LT_CHECK_EQ(c.name(), std::string(""));
    LT_CHECK_EQ(c.value(), std::string(""));
    LT_CHECK_EQ(c.domain(), std::string(""));
    LT_CHECK_EQ(c.path(), std::string(""));
    LT_CHECK_EQ(c.is_secure(), false);
    LT_CHECK_EQ(c.is_http_only(), false);
    LT_CHECK_EQ(c.expires().has_value(), false);
    LT_CHECK_EQ(c.max_age().has_value(), false);
    LT_CHECK_EQ(c.same_site() == same_site_mode::unset, true);
LT_END_AUTO_TEST(cookie_default_constructible_empty)

LT_BEGIN_AUTO_TEST(cookie_header_sentinel_suite, same_site_mode_enum_values_distinct)
    static_assert(static_cast<int>(same_site_mode::unset)
                      != static_cast<int>(same_site_mode::strict),
                  "unset and strict must be distinct");
    static_assert(static_cast<int>(same_site_mode::strict)
                      != static_cast<int>(same_site_mode::lax),
                  "strict and lax must be distinct");
    static_assert(static_cast<int>(same_site_mode::lax)
                      != static_cast<int>(same_site_mode::none),
                  "lax and none must be distinct");
LT_END_AUTO_TEST(same_site_mode_enum_values_distinct)

LT_BEGIN_AUTO_TEST(cookie_header_sentinel_suite, cookie_is_copy_and_move_constructible)
    static_assert(std::is_copy_constructible_v<cookie>,
                  "cookie must be copy-constructible (std::vector<cookie> needs it)");
    static_assert(std::is_copy_assignable_v<cookie>,
                  "cookie must be copy-assignable");
    static_assert(std::is_move_constructible_v<cookie>,
                  "cookie must be move-constructible");
    static_assert(std::is_move_assignable_v<cookie>,
                  "cookie must be move-assignable");
LT_END_AUTO_TEST(cookie_is_copy_and_move_constructible)

LT_BEGIN_AUTO_TEST(cookie_header_sentinel_suite, cookie_is_default_constructible_noexcept)
    static_assert(std::is_nothrow_default_constructible_v<cookie>,
                  "cookie default constructor must be noexcept");
LT_END_AUTO_TEST(cookie_is_default_constructible_noexcept)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
