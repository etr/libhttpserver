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

// TASK-064 Cycle 9-10: http_request::get_cookies_parsed() shape +
// lifetime + per-request cache stability. Uses create_test_request so
// no live MHD daemon is needed.

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "httpserver/create_test_request.hpp"
#include "./littletest.hpp"

using httpserver::cookie;
using httpserver::create_test_request;
using httpserver::http_request;

LT_BEGIN_SUITE(http_request_cookies_parsed_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http_request_cookies_parsed_suite)

LT_BEGIN_AUTO_TEST(http_request_cookies_parsed_suite,
                   return_type_is_const_vector_ref)
    static_assert(std::is_same_v<
        decltype(std::declval<const http_request&>().get_cookies_parsed()),
        const std::vector<cookie>&>,
        "get_cookies_parsed() must return const std::vector<cookie>&");
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(return_type_is_const_vector_ref)

LT_BEGIN_AUTO_TEST(http_request_cookies_parsed_suite,
                   empty_when_no_cookies)
    http_request req =
        create_test_request().path("/").method("GET").build();
    const auto& v = req.get_cookies_parsed();
    LT_CHECK_EQ(v.empty(), true);
LT_END_AUTO_TEST(empty_when_no_cookies)

LT_BEGIN_AUTO_TEST(http_request_cookies_parsed_suite,
                   one_per_cookie_set_via_builder)
    http_request req = create_test_request()
        .path("/").method("GET")
        .cookie("a", "1")
        .cookie("b", "2")
        .build();
    const auto& v = req.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(v[0].name(), std::string("a"));
    LT_CHECK_EQ(v[1].name(), std::string("b"));
LT_END_AUTO_TEST(one_per_cookie_set_via_builder)

LT_BEGIN_AUTO_TEST(http_request_cookies_parsed_suite,
                   preserves_name_and_value)
    http_request req = create_test_request()
        .path("/").method("GET")
        .cookie("sid", "abc123")
        .build();
    const auto& v = req.get_cookies_parsed();
    LT_CHECK_EQ(v.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(v[0].name(), std::string("sid"));
    LT_CHECK_EQ(v[0].value(), std::string("abc123"));
LT_END_AUTO_TEST(preserves_name_and_value)

LT_BEGIN_AUTO_TEST(http_request_cookies_parsed_suite,
                   reference_stable_across_calls)
    http_request req = create_test_request()
        .path("/").method("GET")
        .cookie("sid", "abc")
        .build();
    const std::vector<cookie>* p1 = &req.get_cookies_parsed();
    const std::vector<cookie>* p2 = &req.get_cookies_parsed();
    LT_CHECK_EQ(p1, p2);
LT_END_AUTO_TEST(reference_stable_across_calls)

LT_BEGIN_AUTO_TEST(http_request_cookies_parsed_suite,
                   second_call_does_not_reallocate)
    // This test exercises the cookies_parsed_cache_ guard via the
    // create_test_request() builder path (cookies_parsed_cache_built_
    // is set during build). The live MHD path — where
    // cookies_parsed_cache_built_ is set on the first MHD_get_connection_values
    // call — is only exercised by integration tests (test/integ/).
    // See test-quality-reviewer-iter3-4: the live-MHD branch is accepted
    // as integration-test-only coverage.
    http_request req = create_test_request()
        .path("/").method("GET")
        .cookie("sid", "abc")
        .build();
    // First call builds the cache.
    const auto& v1 = req.get_cookies_parsed();
    const std::size_t cap1 = v1.capacity();
    const cookie* data1 = v1.data();
    // Second call must reuse the same buffer.
    const auto& v2 = req.get_cookies_parsed();
    LT_CHECK_EQ(v2.capacity(), cap1);
    LT_CHECK_EQ(v2.data(), data1);
LT_END_AUTO_TEST(second_call_does_not_reallocate)

LT_BEGIN_AUTO_TEST(http_request_cookies_parsed_suite,
                   idempotent_after_other_getter_calls)
    http_request req = create_test_request()
        .path("/foo").method("GET")
        .cookie("sid", "abc")
        .build();
    // Call an unrelated getter, then get_cookies_parsed twice.
    (void)req.get_path();
    const std::vector<cookie>* p1 = &req.get_cookies_parsed();
    (void)req.get_headers();
    const std::vector<cookie>* p2 = &req.get_cookies_parsed();
    LT_CHECK_EQ(p1, p2);
LT_END_AUTO_TEST(idempotent_after_other_getter_calls)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
