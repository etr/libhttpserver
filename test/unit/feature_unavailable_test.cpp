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

#include <stdexcept>
#include <string>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// AC #1: feature_unavailable derives from std::runtime_error. This compile-time
// assertion runs at TU scope and fires if the inheritance is ever broken.
static_assert(
    std::is_base_of_v<std::runtime_error, httpserver::feature_unavailable>,
    "feature_unavailable must derive from std::runtime_error");

LT_BEGIN_SUITE(feature_unavailable_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(feature_unavailable_suite)

// AC #2: a unit test catches the exception as std::runtime_error and asserts
// that what() contains both the feature name and the build flag.
LT_BEGIN_AUTO_TEST(feature_unavailable_suite,
                   catches_as_runtime_error_with_feature_and_flag)
    std::string msg;
    try {
        throw httpserver::feature_unavailable("tls", "HAVE_GNUTLS");
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    LT_CHECK(msg.find("tls") != std::string::npos);
    LT_CHECK(msg.find("HAVE_GNUTLS") != std::string::npos);
LT_END_AUTO_TEST(catches_as_runtime_error_with_feature_and_flag)

// Catching the concrete type still produces a runtime_error-shaped what().
LT_BEGIN_AUTO_TEST(feature_unavailable_suite, catches_as_feature_unavailable_directly)
    std::string msg;
    try {
        throw httpserver::feature_unavailable("tls", "HAVE_GNUTLS");
    } catch (const httpserver::feature_unavailable& e) {
        const std::runtime_error* base = &e;
        msg = base->what();
    }
    LT_CHECK(msg.find("tls") != std::string::npos);
    LT_CHECK(msg.find("HAVE_GNUTLS") != std::string::npos);
LT_END_AUTO_TEST(catches_as_feature_unavailable_directly)

// Guard against a hard-coded message: a different (feature, flag) pair must
// also propagate verbatim into what().
LT_BEGIN_AUTO_TEST(feature_unavailable_suite, composes_message_for_websocket)
    std::string msg;
    try {
        throw httpserver::feature_unavailable("websocket", "HAVE_WEBSOCKET");
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    LT_CHECK(msg.find("websocket") != std::string::npos);
    LT_CHECK(msg.find("HAVE_WEBSOCKET") != std::string::npos);
LT_END_AUTO_TEST(composes_message_for_websocket)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
