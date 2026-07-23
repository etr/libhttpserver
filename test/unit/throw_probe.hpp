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

#ifndef TEST_UNIT_THROW_PROBE_HPP_
#define TEST_UNIT_THROW_PROBE_HPP_

#include <stdexcept>
#include <utility>

#include "./httpserver.hpp"

namespace httpserver_test {

// Shared throw-type probe for the feature-availability contrast tests
// (webserver_ws_available / webserver_register_ws_smartptr /
// webserver_dauth_available and their *_unavailable twins).
//
// littletest has no ASSERT_THROW, so those suites hand-rolled the same
// dual-bool try/catch block at every site. The idiom lives here once:
// run the callable, record WHICH of the two contract exception types
// (if any) escaped, and let the test body assert the expected verdict
// with plain LT_CHECKs. feature_unavailable must be caught before
// std::invalid_argument in case the hierarchy ever relates them; any
// other exception type propagates out and fails the test loudly rather
// than being misclassified.
struct throw_probe_result {
    bool invalid_argument = false;
    bool feature_unavailable = false;
};

template <typename Fn>
throw_probe_result probe_throw_type(Fn&& fn) {
    throw_probe_result r;
    try {
        std::forward<Fn>(fn)();
    } catch (const httpserver::feature_unavailable&) {
        r.feature_unavailable = true;
    } catch (const std::invalid_argument&) {
        r.invalid_argument = true;
    }
    return r;
}

}  // namespace httpserver_test

#endif  // TEST_UNIT_THROW_PROBE_HPP_
