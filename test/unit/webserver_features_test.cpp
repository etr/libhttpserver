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

// Pins the public contract of httpserver::webserver::features():
//   - returns a struct with exactly four `bool` members in the
//     documented order (basic_auth, digest_auth, tls, websocket);
//   - the function is noexcept;
//   - each field equals the corresponding HAVE_* state at build time.
//
// The test TU is compiled with the same AM_CXXFLAGS as the library,
// so the local #ifdef HAVE_* probes are in lockstep with the library's.

#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// Contract: features struct shape — exactly four bool fields in this order.
static_assert(
    std::is_same_v<decltype(httpserver::webserver::features().basic_auth), bool>,
    "features.basic_auth must be bool");
static_assert(
    std::is_same_v<decltype(httpserver::webserver::features().digest_auth), bool>,
    "features.digest_auth must be bool");
static_assert(
    std::is_same_v<decltype(httpserver::webserver::features().tls), bool>,
    "features.tls must be bool");
static_assert(
    std::is_same_v<decltype(httpserver::webserver::features().websocket), bool>,
    "features.websocket must be bool");

// The struct-tag form disambiguates the type from the homonymous member
// function (both spelled `features`).
static_assert(
    std::is_trivially_copyable_v<struct httpserver::webserver::features>,
    "features must be trivially copyable");

// Contract: the call is noexcept.
static_assert(
    noexcept(httpserver::webserver::features()),
    "webserver::features() must be noexcept");

LT_BEGIN_SUITE(webserver_features_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(webserver_features_suite)

// Compile-time expected values — one #ifdef per flag, declared outside the
// test body so there is no internal branching inside the test assertion
// sequence.
#ifdef HAVE_BAUTH
constexpr bool k_expected_bauth = true;
#else
constexpr bool k_expected_bauth = false;
#endif
#ifdef HAVE_DAUTH
constexpr bool k_expected_dauth = true;
#else
constexpr bool k_expected_dauth = false;
#endif
#ifdef HAVE_GNUTLS
constexpr bool k_expected_tls = true;
#else
constexpr bool k_expected_tls = false;
#endif
#ifdef HAVE_WEBSOCKET
constexpr bool k_expected_ws = true;
#else
constexpr bool k_expected_ws = false;
#endif

// Contract: each field reflects the HAVE_* that the library was built with.
// The four assertions below are unconditional: each compares the runtime
// value against its compile-time expected constant so the intent is clear
// and both true and false branches are visible in every build.
LT_BEGIN_AUTO_TEST(webserver_features_suite, fields_match_build_flags)
    auto f = httpserver::webserver::features();
    LT_CHECK_EQ(f.basic_auth, k_expected_bauth);
    LT_CHECK_EQ(f.digest_auth, k_expected_dauth);
    LT_CHECK_EQ(f.tls, k_expected_tls);
    LT_CHECK_EQ(f.websocket, k_expected_ws);
LT_END_AUTO_TEST(fields_match_build_flags)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
