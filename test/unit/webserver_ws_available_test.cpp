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

// TASK-081 cycle 1 — paired on-path companion to
// webserver_ws_unavailable_test.cpp.
//
// webserver_ws_unavailable_test.cpp pins the HAVE_WEBSOCKET-OFF throw-site
// contract (feature_unavailable naming "websocket" and "HAVE_WEBSOCKET"
// for every public WebSocket entry point). On a HAVE_WEBSOCKET-ON build
// that suite is intentionally empty, so this paired TU pins the
// complementary ON-path contracts:
//
//   1. webserver::features().websocket == true on this build (locks the
//      flag-state to the same TU that owns the WS-available contracts).
//   2. register_ws_resource(unique_ptr) with a null argument throws
//      std::invalid_argument (NOT feature_unavailable). The throw-type
//      contrast is the point: under HAVE_WEBSOCKET-off the same null
//      argument trips feature_unavailable (exercised by the unavailable
//      TU); under HAVE_WEBSOCKET-on it must trip the generic null-guard
//      from webserver_routes.cpp (mirrors TASK-035 / TASK-023).
//   3. register_ws_resource(shared_ptr) with a null argument throws
//      std::invalid_argument from the same null-guard.
//
// On a HAVE_WEBSOCKET-off build there is no contract to pin from this TU
// (the WS-off contracts live in webserver_ws_unavailable_test.cpp), so
// the suite is left empty there: the binary still exists, runs, exits 0,
// and contributes to the make-check totals in both configurations.
//
// (The richer runtime ownership contracts — dtor counting, duplicate
// throws, unregister round-trips — already live in
// webserver_register_ws_smartptr_test.cpp and are deliberately not
// duplicated here; this TU's value-add is the throw-type contrast against
// the unavailable TU.)

#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./unit/throw_probe.hpp"

LT_BEGIN_SUITE(webserver_ws_available_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(webserver_ws_available_suite)

#ifdef HAVE_WEBSOCKET
LT_BEGIN_AUTO_TEST(webserver_ws_available_suite,
                   features_reports_websocket_on)
    const auto f = httpserver::webserver::features();
    LT_CHECK_EQ(f.websocket, true);
LT_END_AUTO_TEST(features_reports_websocket_on)

// Throw-type contrast against webserver_ws_unavailable_test.cpp:
//   - HAVE_WEBSOCKET-OFF: null unique_ptr → feature_unavailable.
//   - HAVE_WEBSOCKET-ON:  null unique_ptr → std::invalid_argument
//     (from the generic null-guard in webserver_routes.cpp).
// If the throw routing ever regresses to feature_unavailable on the
// ON-build, the unavailable test would still pass on the OFF lane but
// the unique_ptr_null_throws_invalid_argument check here would fail.
// Paired with shared_ptr_null_throws_invalid_argument below (same
// null-guard contract, unique_ptr vs. shared_ptr overload).
LT_BEGIN_AUTO_TEST(webserver_ws_available_suite,
                   unique_ptr_null_throws_invalid_argument)
    httpserver::webserver ws{
        httpserver::create_webserver(8190).listen_socket(false)};
    const auto verdict = httpserver_test::probe_throw_type([&ws] {
        ws.register_ws_resource(
            "/ws", std::unique_ptr<httpserver::websocket_handler>{});
    });
    LT_CHECK(verdict.invalid_argument);
    LT_CHECK(!verdict.feature_unavailable);
LT_END_AUTO_TEST(unique_ptr_null_throws_invalid_argument)

// Paired with unique_ptr_null_throws_invalid_argument above (same
// null-guard contract, unique_ptr vs. shared_ptr overload).
LT_BEGIN_AUTO_TEST(webserver_ws_available_suite,
                   shared_ptr_null_throws_invalid_argument)
    httpserver::webserver ws{
        httpserver::create_webserver(8191).listen_socket(false)};
    const auto verdict = httpserver_test::probe_throw_type([&ws] {
        ws.register_ws_resource(
            "/ws", std::shared_ptr<httpserver::websocket_handler>{});
    });
    LT_CHECK(verdict.invalid_argument);
    LT_CHECK(!verdict.feature_unavailable);
LT_END_AUTO_TEST(shared_ptr_null_throws_invalid_argument)
#endif  // HAVE_WEBSOCKET

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
