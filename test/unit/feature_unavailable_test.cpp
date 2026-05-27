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
        // nothing to set up
    }

    void tear_down() {
        // nothing to tear down
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
    LT_CHECK(!msg.empty());
    LT_CHECK(msg.find("tls") != std::string::npos);
    LT_CHECK(msg.find("HAVE_GNUTLS") != std::string::npos);
LT_END_AUTO_TEST(catches_as_runtime_error_with_feature_and_flag)

// AC #3: the polymorphism chain extends to std::exception — catching as the
// common root still delivers an intact what() message, confirming that the
// exception is not sliced and the hierarchy is fully connected.
LT_BEGIN_AUTO_TEST(feature_unavailable_suite, catches_as_std_exception)
    std::string msg;
    try {
        throw httpserver::feature_unavailable("tls", "HAVE_GNUTLS");
    } catch (const std::exception& e) {
        msg = e.what();
    }
    LT_CHECK(!msg.empty());
    LT_CHECK(msg.find("tls") != std::string::npos);
    LT_CHECK(msg.find("HAVE_GNUTLS") != std::string::npos);
LT_END_AUTO_TEST(catches_as_std_exception)

// Guard against a hard-coded message: a different (feature, flag) pair must
// also propagate verbatim into what().
LT_BEGIN_AUTO_TEST(feature_unavailable_suite, composes_message_for_websocket)
    std::string msg;
    try {
        throw httpserver::feature_unavailable("websocket", "HAVE_WEBSOCKET");
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    LT_CHECK(!msg.empty());
    LT_CHECK(msg.find("websocket") != std::string::npos);
    LT_CHECK(msg.find("HAVE_WEBSOCKET") != std::string::npos);
LT_END_AUTO_TEST(composes_message_for_websocket)

// Edge-case: empty feature name and empty build flag. The fixed surrounding
// text ("feature '' unavailable: built without ") ensures what() is always
// non-empty and well-formed regardless of the arguments.
LT_BEGIN_AUTO_TEST(feature_unavailable_suite, empty_args_produce_non_empty_message)
    std::string msg;
    try {
        throw httpserver::feature_unavailable("", "");
    } catch (const std::runtime_error& e) {
        msg = e.what();
    }
    LT_CHECK(!msg.empty());
LT_END_AUTO_TEST(empty_args_produce_non_empty_message)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
