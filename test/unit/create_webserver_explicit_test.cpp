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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301  USA
*/

// TASK-030 compile-time sentinel.
// Pins two API guarantees:
//   (1) PRD-NAM-REQ-004: webserver(const create_webserver&) is explicit.
//       Implicit conversion (`webserver w = some_create_webserver;`) must
//       not compile; explicit direct-init (`webserver w(cw);`) must.
//   (2) PRD-NAM-REQ-003: the three error-page setters carry the _handler
//       suffix and take std::function<http_response(const http_request&)>.
//       The legacy _resource-suffixed setters are removed entirely.

#include <functional>
#include <string_view>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;
using error_handler_fn = std::function<ht::http_response(const ht::http_request&)>;
// TASK-031 / DR-009 §5.2: internal_error_handler now takes a second
// argument carrying the originating exception's message.
using internal_error_handler_fn =
    std::function<ht::http_response(const ht::http_request&, std::string_view)>;

// (1a) Implicit conversion from create_webserver to webserver is forbidden.
static_assert(!std::is_convertible_v<ht::create_webserver, ht::webserver>,
              "webserver(const create_webserver&) must be explicit");

// (1b) Explicit direct-init from create_webserver still works.
static_assert(std::is_constructible_v<ht::webserver, const ht::create_webserver&>,
              "webserver must remain constructible from create_webserver");

// (2) SFINAE detectors for the renamed setters.
// Positive: the new _handler-suffixed setters exist with the by-value
// http_response signature.
template <typename, typename = void>
struct has_not_found_handler : std::false_type {};
template <typename CW>
struct has_not_found_handler<CW, std::void_t<
    decltype(std::declval<CW&>().not_found_handler(std::declval<error_handler_fn>()))>>
    : std::true_type {};

template <typename, typename = void>
struct has_method_not_allowed_handler : std::false_type {};
template <typename CW>
struct has_method_not_allowed_handler<CW, std::void_t<
    decltype(std::declval<CW&>().method_not_allowed_handler(std::declval<error_handler_fn>()))>>
    : std::true_type {};

template <typename, typename = void>
struct has_internal_error_handler : std::false_type {};
template <typename CW>
struct has_internal_error_handler<CW, std::void_t<
    decltype(std::declval<CW&>().internal_error_handler(std::declval<internal_error_handler_fn>()))>>
    : std::true_type {};

// TASK-031: pin that the OLD single-arg signature is NOT accepted -- the
// widening to (request, message) is a hard API change.
template <typename, typename = void>
struct accepts_old_internal_error_handler : std::false_type {};
template <typename CW>
struct accepts_old_internal_error_handler<CW, std::void_t<
    decltype(std::declval<CW&>().internal_error_handler(std::declval<error_handler_fn>()))>>
    : std::true_type {};

static_assert(has_not_found_handler<ht::create_webserver>::value,
              "create_webserver::not_found_handler(handler_fn) must exist");
static_assert(has_method_not_allowed_handler<ht::create_webserver>::value,
              "create_webserver::method_not_allowed_handler(handler_fn) must exist");
static_assert(has_internal_error_handler<ht::create_webserver>::value,
              "create_webserver::internal_error_handler(request, message) must exist");
static_assert(!accepts_old_internal_error_handler<ht::create_webserver>::value,
              "create_webserver::internal_error_handler must reject the legacy "
              "single-arg signature per TASK-031 / DR-009");

// Negative: the legacy _resource-suffixed setters must be gone.
template <typename T> struct always_void { using type = void; };

template <typename, typename = void>
struct has_not_found_resource : std::false_type {};
template <typename CW>
struct has_not_found_resource<CW, std::void_t<
    decltype(std::declval<CW&>().not_found_resource(std::declval<error_handler_fn>()))>>
    : std::true_type {};

template <typename, typename = void>
struct has_method_not_allowed_resource : std::false_type {};
template <typename CW>
struct has_method_not_allowed_resource<CW, std::void_t<
    decltype(std::declval<CW&>().method_not_allowed_resource(std::declval<error_handler_fn>()))>>
    : std::true_type {};

template <typename, typename = void>
struct has_internal_error_resource : std::false_type {};
template <typename CW>
struct has_internal_error_resource<CW, std::void_t<
    decltype(std::declval<CW&>().internal_error_resource(std::declval<error_handler_fn>()))>>
    : std::true_type {};

static_assert(!has_not_found_resource<ht::create_webserver>::value,
              "create_webserver::not_found_resource must be removed");
static_assert(!has_method_not_allowed_resource<ht::create_webserver>::value,
              "create_webserver::method_not_allowed_resource must be removed");
static_assert(!has_internal_error_resource<ht::create_webserver>::value,
              "create_webserver::internal_error_resource must be removed");

LT_BEGIN_SUITE(create_webserver_explicit_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(create_webserver_explicit_suite)

LT_BEGIN_AUTO_TEST(create_webserver_explicit_suite, smoke)
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(smoke)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
