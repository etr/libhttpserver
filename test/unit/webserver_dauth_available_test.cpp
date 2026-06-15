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

// TASK-081 cycle 2 — paired on-path companion to
// webserver_dauth_unavailable_test.cpp.
//
// webserver_dauth_unavailable_test.cpp pins the HAVE_DAUTH-OFF throw-site
// contract (constructing a webserver with digest_auth(true) throws
// feature_unavailable naming "digest_auth" and "HAVE_DAUTH"). On a
// HAVE_DAUTH-ON build that suite is intentionally empty, so this paired
// TU pins the complementary ON-path contracts:
//
//   1. webserver::features().digest_auth == true on this build.
//   2. Constructing a webserver with digest_auth(true) does NOT throw
//      feature_unavailable — the throw-type contrast that makes the
//      unavailable TU's contract meaningful.
//   3. An *explicit* digest_auth(false) also constructs cleanly on a
//      HAVE_DAUTH-on build (guards against an accidental "any explicit
//      setter call throws" regression).
//
// On a HAVE_DAUTH-off build there is no contract to pin from this TU
// (the off-path contracts live in webserver_dauth_unavailable_test.cpp),
// so the suite is left empty there: the binary still exists, runs, exits
// 0, and contributes to the make-check totals in both configurations.

#include "./httpserver.hpp"
#include "./littletest.hpp"

LT_BEGIN_SUITE(webserver_dauth_available_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(webserver_dauth_available_suite)

#ifdef HAVE_DAUTH
LT_BEGIN_AUTO_TEST(webserver_dauth_available_suite,
                   features_reports_digest_auth_on)
    const auto f = httpserver::webserver::features();
    LT_CHECK_EQ(f.digest_auth, true);
LT_END_AUTO_TEST(features_reports_digest_auth_on)

// Throw-type contrast against webserver_dauth_unavailable_test.cpp:
//   - HAVE_DAUTH-OFF: digest_auth(true) → feature_unavailable.
//   - HAVE_DAUTH-ON:  digest_auth(true) constructs cleanly.
// listen_socket(false) keeps the test port-safe (no real bind).
LT_BEGIN_AUTO_TEST(webserver_dauth_available_suite,
                   digest_auth_true_does_not_throw_on_dauth_on)
    bool caught_feature_unavailable = false;
    try {
        httpserver::webserver ws{
            httpserver::create_webserver(8194)
                .digest_auth(true)
                .listen_socket(false)};
        (void)ws;
    } catch (const httpserver::feature_unavailable&) {
        caught_feature_unavailable = true;
    }
    LT_CHECK(!caught_feature_unavailable);
LT_END_AUTO_TEST(digest_auth_true_does_not_throw_on_dauth_on)

// Explicit digest_auth(false) on a HAVE_DAUTH-on build must construct
// cleanly — guards against an accidental "any explicit setter call
// throws" regression that the unavailable TU's
// explicit_digest_auth_false_does_not_throw_on_dauth_off test cannot
// catch (its #ifndef HAVE_DAUTH gate makes it dormant here).
LT_BEGIN_AUTO_TEST(webserver_dauth_available_suite,
                   explicit_digest_auth_false_does_not_throw_on_dauth_on)
    bool caught_feature_unavailable = false;
    try {
        httpserver::webserver ws{
            httpserver::create_webserver(8195)
                .digest_auth(false)
                .listen_socket(false)};
        (void)ws;
    } catch (const httpserver::feature_unavailable&) {
        caught_feature_unavailable = true;
    }
    LT_CHECK(!caught_feature_unavailable);
LT_END_AUTO_TEST(explicit_digest_auth_false_does_not_throw_on_dauth_on)
#endif  // HAVE_DAUTH

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
