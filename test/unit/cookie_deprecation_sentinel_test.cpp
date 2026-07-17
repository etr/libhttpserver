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

// [[deprecated]] attribute pin for the legacy cookie surface.
//
// Compile-only sentinel verifying that the legacy string-blob cookie
// surface on http_response still compiles -- but does so behind a
// localized #pragma diagnostic suppression block (because the rest of
// this TU is built with -Wall and would trip [[deprecated]] otherwise).
//
// The negative side -- that the warning IS emitted on a flag-free
// compile -- is verified mechanically by the
// lint-deprecated-cookie-overload gate
// (scripts/check-deprecated-cookie-overload.sh, wired into `make check`
// via check-local): it compiles a TU calling with_cookie(string,string)
// with -Werror=deprecated-declarations and asserts the compile FAILS
// with a deprecation diagnostic (plus a positive control proving the
// structured overload builds cleanly). Removing the [[deprecated]]
// attribute therefore breaks `make check`, not just this comment.
//
// Pure compile test -- empty LDADD.

#include <string>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::http_response;

LT_BEGIN_SUITE(cookie_deprecation_sentinel_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(cookie_deprecation_sentinel_suite)

LT_BEGIN_AUTO_TEST(cookie_deprecation_sentinel_suite, legacy_with_cookie_still_compiles)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    http_response r = http_response::string("body");
    r.with_cookie("sid", "abc");
    LT_CHECK_EQ(r.get_cookie("sid"), std::string("abc"));
#pragma GCC diagnostic pop
LT_END_AUTO_TEST(legacy_with_cookie_still_compiles)

LT_BEGIN_AUTO_TEST(cookie_deprecation_sentinel_suite, legacy_get_cookies_return_type_unchanged)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    http_response r;
    static_assert(std::is_same_v<
        decltype(r.get_cookies()),
        const httpserver::http::header_map&>,
        "legacy get_cookies() must still return const header_map&");
    LT_CHECK_EQ(true, true);
#pragma GCC diagnostic pop
LT_END_AUTO_TEST(legacy_get_cookies_return_type_unchanged)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
