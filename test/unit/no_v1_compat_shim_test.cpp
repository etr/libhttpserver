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

// TASK-067 -- Sentinel: the v1 compat auth-handler shim is gone.
//
// In v2.0 the canonical auth_handler_ptr accepts only
//   std::function<std::optional<http_response>(const http_request&)>.
//
// During the v2.0 transitional window (TASK-054) a deprecated overload
// at create_webserver::auth_handler(compat::auth_handler_v1_ptr) and a
// namespace httpserver::compat shim accepted the v1 shape
//   std::function<std::shared_ptr<http_response>(const http_request&)>
// to spare in-flight callers a hard build break. The shim was scheduled
// for removal "in the next release"; this TU pins that removal.
//
// SFINAE probe: a call to
//   create_webserver{}.auth_handler(legacy_sig{})
// must NOT compile after TASK-067. We check the negative case at
// compile time with std::void_t and a detection idiom.
//
// If this static_assert ever fires, somebody re-introduced an overload
// that accepts the v1 shared_ptr-returning callable -- either by
// resurrecting the compat::auth_handler_v1_ptr typedef, by adding a
// generic templated forwarder, or by an implicit conversion. Any of
// those would silently re-open the bug DR-009 closes.

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

namespace {

// Detection idiom: accepts<CW, F>::value is true iff
// declval<CW&>().auth_handler(declval<F>()) is a well-formed expression.
template <class CW, class F, class = void>
struct accepts_legacy : std::false_type {};

template <class CW, class F>
struct accepts_legacy<CW, F, std::void_t<
decltype(std::declval<CW&>().auth_handler(std::declval<F>()))>>
: std::true_type {};

// The v1 shape: shared_ptr-returning auth callable. The TASK-054
// transitional overload bound this shape; with TASK-067 done, no
// auth_handler overload should accept it.
using legacy_sig = std::function<
std::shared_ptr<httpserver::http_response>(const httpserver::http_request&)>;

static_assert(!accepts_legacy<httpserver::create_webserver, legacy_sig>::value,
"TASK-067: create_webserver::auth_handler must not accept the v1 "
"std::shared_ptr<http_response> callable shape. The compat shim was "
"scheduled for removal in v2.1; if you intentionally re-introduced it, "
"update this sentinel.");

// And a positive control: the canonical v2 shape MUST still be accepted.
using canonical_sig = std::function<
std::optional<httpserver::http_response>(const httpserver::http_request&)>;
static_assert(accepts_legacy<httpserver::create_webserver, canonical_sig>::value,
"TASK-067: create_webserver::auth_handler must accept the canonical "
"std::optional<http_response> callable shape.");

}  // namespace

LT_BEGIN_SUITE(no_v1_compat_shim_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(no_v1_compat_shim_suite)

// Runtime trivia test: the static_asserts above are the load-bearing
// pin. This LT case exists so littletest's test-runner main sees the
// suite, prints a green line, and the file participates in the
// `make check` aggregate. The CI value is the compile-time assertion;
// the runtime check confirms the TU links into the test runner.
LT_BEGIN_AUTO_TEST(no_v1_compat_shim_suite, sentinel_compiles)
    LT_CHECK_EQ(1, 1);
LT_END_AUTO_TEST(sentinel_compiles)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
