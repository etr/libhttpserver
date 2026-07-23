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

// Digest-auth gating on HAVE_DAUTH-off builds:
//
// On a HAVE_DAUTH-off build the webserver constructor must throw
// feature_unavailable (naming "digest_auth" and "HAVE_DAUTH") when
// digest_auth_enabled is true in the create_webserver params
// (PRD-FLG-REQ-001 / CWE-287).
//
// Additionally, a default-constructed create_webserver (without an explicit
// .digest_auth(true) call) must NOT throw on a HAVE_DAUTH-off build.
// This requires webserver_config::digest_auth_enabled to default to false
// on HAVE_DAUTH-off builds, which is achieved via
// detail::default_digest_auth_enabled() defined in create_webserver.cpp —
// the same pattern used for detail::default_basic_auth_enabled().
//
// On HAVE_DAUTH-on builds this suite is left empty: the behavioural
// contract (webserver construction succeeds) is already exercised by
// the integration tests. The binary still exists, runs, and exits 0 in
// both configurations.

#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"

LT_BEGIN_SUITE(webserver_dauth_unavailable_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(webserver_dauth_unavailable_suite)

#ifndef HAVE_DAUTH
// PRD-FLG-REQ-001: on a HAVE_DAUTH-off build, constructing a webserver
// with digest_auth(true) must throw feature_unavailable whose what()
// names both "digest_auth" and "HAVE_DAUTH".
LT_BEGIN_AUTO_TEST(webserver_dauth_unavailable_suite,
                   ctor_throws_feature_unavailable_when_digest_auth_requested)
    bool caught = false;
    std::string msg;
    try {
        httpserver::webserver ws{
            httpserver::create_webserver(8091).digest_auth(true)};
        (void)ws;
    } catch (const httpserver::feature_unavailable& e) {
        caught = true;
        msg = e.what();
    }
    LT_CHECK(caught);
    LT_CHECK(msg.find("digest_auth") != std::string::npos);
    LT_CHECK(msg.find("HAVE_DAUTH") != std::string::npos);
LT_END_AUTO_TEST(ctor_throws_feature_unavailable_when_digest_auth_requested)

// PRD-FLG-REQ-001 complementary: a default create_webserver (no
// explicit .digest_auth(true)) must NOT throw on a HAVE_DAUTH-off build.
// This pins that webserver_config::digest_auth_enabled defaults to false
// via detail::default_digest_auth_enabled(), not to the hardcoded true.
// listen_socket(false) avoids binding a real port so the test is
// port-safe in CI.
LT_BEGIN_AUTO_TEST(webserver_dauth_unavailable_suite,
                   default_ctor_does_not_throw_on_dauth_off)
    bool threw = false;
    try {
        httpserver::webserver ws{
            httpserver::create_webserver(8092).listen_socket(false)};
        (void)ws;
    } catch (const httpserver::feature_unavailable&) {
        threw = true;
    }
    LT_CHECK(!threw);
LT_END_AUTO_TEST(default_ctor_does_not_throw_on_dauth_off)

// PRD-FLG-REQ-001 third path: an *explicit* .digest_auth(false) must also
// not throw on a HAVE_DAUTH-off build — pinning that the guard fires only
// on true, not on every non-default value of the setter.
LT_BEGIN_AUTO_TEST(webserver_dauth_unavailable_suite,
                   explicit_digest_auth_false_does_not_throw_on_dauth_off)
    bool threw = false;
    try {
        httpserver::webserver ws{
            httpserver::create_webserver(8093).digest_auth(false).listen_socket(false)};
        (void)ws;
    } catch (const httpserver::feature_unavailable&) {
        threw = true;
    }
    LT_CHECK(!threw);
LT_END_AUTO_TEST(explicit_digest_auth_false_does_not_throw_on_dauth_off)
#endif  // !HAVE_DAUTH

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
