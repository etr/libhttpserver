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

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// AC: every value-constant from v1's #define wall is now visible as a
// constexpr symbol under httpserver::constants when consumers include
// <httpserver.hpp>. These compile-time assertions are the contract.
static_assert(httpserver::constants::DEFAULT_WS_PORT == 9898,
              "DEFAULT_WS_PORT must equal 9898 (v1 default)");
static_assert(httpserver::constants::DEFAULT_WS_TIMEOUT == 180,
              "DEFAULT_WS_TIMEOUT must equal 180 seconds (v1 default)");
static_assert(httpserver::constants::DEFAULT_MASK_VALUE == 0xFFFFu,
              "DEFAULT_MASK_VALUE must equal 0xFFFF (v1 default)");
static_assert(httpserver::constants::NOT_FOUND_ERROR ==
                  std::string_view{"Not Found"},
              "NOT_FOUND_ERROR text must match v1 default body");
static_assert(httpserver::constants::METHOD_ERROR ==
                  std::string_view{"Method not Allowed"},
              "METHOD_ERROR text must match v1 default body");
static_assert(httpserver::constants::NOT_METHOD_ERROR ==
                  std::string_view{"Method not Acceptable"},
              "NOT_METHOD_ERROR text must match v1 default body");
static_assert(httpserver::constants::GENERIC_ERROR ==
                  std::string_view{"Internal Error"},
              "GENERIC_ERROR text must match v1 default body");

// AC: types are pinned. Numeric ports/masks are uint16_t; messages are
// std::string_view (no allocation, std::string-constructible at call sites).
// std::remove_cv_t strips the const that `constexpr` adds to the symbol.
static_assert(std::is_same_v<std::remove_cv_t<decltype(
                                  httpserver::constants::DEFAULT_WS_PORT)>,
                             std::uint16_t>,
              "DEFAULT_WS_PORT must be std::uint16_t");
static_assert(std::is_same_v<std::remove_cv_t<decltype(
                                  httpserver::constants::DEFAULT_MASK_VALUE)>,
                             std::uint16_t>,
              "DEFAULT_MASK_VALUE must be std::uint16_t");
static_assert(std::is_same_v<std::remove_cv_t<decltype(
                                  httpserver::constants::DEFAULT_WS_TIMEOUT)>,
                             int>,
              "DEFAULT_WS_TIMEOUT must be int (matches "
              "create_webserver._connection_timeout field)");
static_assert(std::is_same_v<std::remove_cv_t<decltype(
                                  httpserver::constants::NOT_FOUND_ERROR)>,
                             std::string_view>,
              "NOT_FOUND_ERROR must be std::string_view");
static_assert(std::is_same_v<std::remove_cv_t<decltype(
                                  httpserver::constants::METHOD_ERROR)>,
                             std::string_view>,
              "METHOD_ERROR must be std::string_view");
static_assert(std::is_same_v<std::remove_cv_t<decltype(
                                  httpserver::constants::NOT_METHOD_ERROR)>,
                             std::string_view>,
              "NOT_METHOD_ERROR must be std::string_view");
static_assert(std::is_same_v<std::remove_cv_t<decltype(
                                  httpserver::constants::GENERIC_ERROR)>,
                             std::string_view>,
              "GENERIC_ERROR must be std::string_view");

// AC: the v1 #define names must NOT leak into consumer namespace after
// #include <httpserver.hpp>. This is the public-header-gate witness:
// if any of these macros is still #define'd, this TU fails to preprocess.
// Same idiom as test/unit/header_hygiene_iovec_test.cpp's _SYS_UIO_H check.
#ifdef DEFAULT_WS_PORT
#  error "DEFAULT_WS_PORT macro must not leak after #include <httpserver.hpp>"
#endif
#ifdef DEFAULT_WS_TIMEOUT
#  error "DEFAULT_WS_TIMEOUT macro must not leak after #include <httpserver.hpp>"
#endif
#ifdef DEFAULT_MASK_VALUE
#  error "DEFAULT_MASK_VALUE macro must not leak after #include <httpserver.hpp>"
#endif
#ifdef NOT_FOUND_ERROR
#  error "NOT_FOUND_ERROR macro must not leak after #include <httpserver.hpp>"
#endif
#ifdef METHOD_ERROR
#  error "METHOD_ERROR macro must not leak after #include <httpserver.hpp>"
#endif
#ifdef NOT_METHOD_ERROR
#  error "NOT_METHOD_ERROR macro must not leak after #include <httpserver.hpp>"
#endif
#ifdef GENERIC_ERROR
#  error "GENERIC_ERROR macro must not leak after #include <httpserver.hpp>"
#endif

LT_BEGIN_SUITE(constants_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(constants_suite)

// Runtime checks mirror the static_asserts so failures show up readably in
// CI logs (a static_assert breaks the build with a message; the runtime
// check produces a labelled "passed" line in the test runner).
LT_BEGIN_AUTO_TEST(constants_suite, default_ws_port_value)
    LT_CHECK_EQ(httpserver::constants::DEFAULT_WS_PORT, 9898);
LT_END_AUTO_TEST(default_ws_port_value)

LT_BEGIN_AUTO_TEST(constants_suite, default_ws_timeout_value)
    LT_CHECK_EQ(httpserver::constants::DEFAULT_WS_TIMEOUT, 180);
LT_END_AUTO_TEST(default_ws_timeout_value)

LT_BEGIN_AUTO_TEST(constants_suite, default_mask_value)
    LT_CHECK_EQ(httpserver::constants::DEFAULT_MASK_VALUE, 0xFFFFu);
LT_END_AUTO_TEST(default_mask_value)

LT_BEGIN_AUTO_TEST(constants_suite, not_found_error_text)
    LT_CHECK_EQ(httpserver::constants::NOT_FOUND_ERROR,
                std::string_view{"Not Found"});
LT_END_AUTO_TEST(not_found_error_text)

LT_BEGIN_AUTO_TEST(constants_suite, method_error_text)
    LT_CHECK_EQ(httpserver::constants::METHOD_ERROR,
                std::string_view{"Method not Allowed"});
LT_END_AUTO_TEST(method_error_text)

LT_BEGIN_AUTO_TEST(constants_suite, not_method_error_text)
    LT_CHECK_EQ(httpserver::constants::NOT_METHOD_ERROR,
                std::string_view{"Method not Acceptable"});
LT_END_AUTO_TEST(not_method_error_text)

LT_BEGIN_AUTO_TEST(constants_suite, generic_error_text)
    LT_CHECK_EQ(httpserver::constants::GENERIC_ERROR,
                std::string_view{"Internal Error"});
LT_END_AUTO_TEST(generic_error_text)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
