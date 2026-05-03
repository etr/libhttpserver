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

#include <string>

#include "./httpserver.hpp"
#include "httpserver/detail/modded_request.hpp"

#include "./littletest.hpp"

// uri_log is the MHD URI-log callback defined in src/webserver.cpp. It is
// exported from the library but has no public header, so we re-declare its
// signature here. MHD_Connection is opaque to this test - we only ever pass
// nullptr, mirroring how MHD itself may invoke the callback before the
// connection is fully initialised.
namespace httpserver {
void* uri_log(void* cls, const char* uri, struct MHD_Connection* con);
}  // namespace httpserver

LT_BEGIN_SUITE(uri_log_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(uri_log_suite)

// Regression test for issue #371: under load (port scans, half-open
// connections, non-HTTP traffic on the listening port) MHD may invoke the
// URI-log callback with a null uri pointer before the request line has
// been parsed. The previous implementation assigned the raw pointer into
// std::string, which throws std::logic_error and aborts the process via
// std::terminate because the throw escapes a C callback.
LT_BEGIN_AUTO_TEST(uri_log_suite, null_uri_does_not_throw)
    void* raw = nullptr;
    LT_CHECK_NOTHROW(raw = httpserver::uri_log(nullptr, nullptr, nullptr));
    LT_CHECK(raw != nullptr);

    auto* mr = static_cast<httpserver::detail::modded_request*>(raw);
    LT_CHECK_EQ(mr->complete_uri, std::string(""));
    delete mr;
LT_END_AUTO_TEST(null_uri_does_not_throw)

// Sanity check that the happy path still records the URI as before.
LT_BEGIN_AUTO_TEST(uri_log_suite, valid_uri_is_stored)
    const char* uri = "/some/path?with=query";
    void* raw = httpserver::uri_log(nullptr, uri, nullptr);
    LT_CHECK(raw != nullptr);

    auto* mr = static_cast<httpserver::detail::modded_request*>(raw);
    LT_CHECK_EQ(mr->complete_uri, std::string(uri));
    delete mr;
LT_END_AUTO_TEST(valid_uri_is_stored)

// Empty (but non-null) URI should be stored verbatim - this is the same
// observable state the null-uri path now produces, so route matching falls
// through to a 404 in both cases.
LT_BEGIN_AUTO_TEST(uri_log_suite, empty_uri_is_stored)
    void* raw = httpserver::uri_log(nullptr, "", nullptr);
    LT_CHECK(raw != nullptr);

    auto* mr = static_cast<httpserver::detail::modded_request*>(raw);
    LT_CHECK_EQ(mr->complete_uri, std::string(""));
    delete mr;
LT_END_AUTO_TEST(empty_uri_is_stored)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
