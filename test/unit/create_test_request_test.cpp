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

#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/create_test_request.hpp"
#include "httpserver/detail/body.hpp"
#include "./littletest.hpp"

using httpserver::create_test_request;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;

// Test-only accessor for http_response internals (same pattern as
// http_response_sbo_test.cpp and http_response_factories_test.cpp).
namespace httpserver {
struct http_response_sbo_test_access {
    static bool body_inline(http_response& r) noexcept {
        return r.body_inline_;
    }
    static httpserver::detail::body* body_ptr(http_response& r) noexcept {
        return r.body_;
    }
    static body_kind kind(http_response& r) noexcept { return r.kind_; }
};
}  // namespace httpserver

namespace {
using SBO = httpserver::http_response_sbo_test_access;
}  // namespace

LT_BEGIN_SUITE(create_test_request_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(create_test_request_suite)

// Test default values
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_default)
    auto req = create_test_request().build();
    LT_CHECK_EQ(std::string(req.get_method()), std::string("GET"));
    LT_CHECK_EQ(std::string(req.get_path()), std::string("/"));
    LT_CHECK_EQ(std::string(req.get_version()), std::string("HTTP/1.1"));
    LT_CHECK_EQ(std::string(req.get_content()), std::string(""));
LT_END_AUTO_TEST(build_default)

// Test custom method and path
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_method_path)
    auto req = create_test_request()
        .method("POST")
        .path("/api/users")
        .build();
    LT_CHECK_EQ(std::string(req.get_method()), std::string("POST"));
    LT_CHECK_EQ(std::string(req.get_path()), std::string("/api/users"));
LT_END_AUTO_TEST(build_method_path)

// Test headers
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_headers)
    auto req = create_test_request()
        .header("Content-Type", "application/json")
        .header("Accept", "text/plain")
        .build();
    LT_CHECK_EQ(std::string(req.get_header("Content-Type")), std::string("application/json"));
    LT_CHECK_EQ(std::string(req.get_header("Accept")), std::string("text/plain"));
    LT_CHECK_EQ(std::string(req.get_header("NonExistent")), std::string(""));

    // TASK-017: get_headers() returns const&; bind by reference.
    const auto& headers = req.get_headers();
    LT_CHECK_EQ(headers.size(), static_cast<size_t>(2));
LT_END_AUTO_TEST(build_headers)

// Test footers and cookies
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_footers_cookies)
    auto req = create_test_request()
        .footer("X-Checksum", "abc123")
        .cookie("session_id", "xyz789")
        .build();
    LT_CHECK_EQ(std::string(req.get_footer("X-Checksum")), std::string("abc123"));
    LT_CHECK_EQ(std::string(req.get_cookie("session_id")), std::string("xyz789"));

    // TASK-017: get_footers/get_cookies() return const&; bind by reference.
    const auto& footers = req.get_footers();
    LT_CHECK_EQ(footers.size(), static_cast<size_t>(1));

    const auto& cookies = req.get_cookies();
    LT_CHECK_EQ(cookies.size(), static_cast<size_t>(1));
LT_END_AUTO_TEST(build_footers_cookies)

// Test args
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_args)
    auto req = create_test_request()
        .arg("name", "World")
        .arg("lang", "en")
        .build();
    LT_CHECK_EQ(std::string(req.get_arg_flat("name")), std::string("World"));
    LT_CHECK_EQ(std::string(req.get_arg_flat("lang")), std::string("en"));
LT_END_AUTO_TEST(build_args)

// Test multiple values per arg key
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_multi_args)
    auto req = create_test_request()
        .arg("color", "red")
        .arg("color", "blue")
        .build();
    auto arg = req.get_arg("color");
    LT_CHECK_EQ(arg.values.size(), static_cast<size_t>(2));
    LT_CHECK_EQ(std::string(arg.values[0]), std::string("red"));
    LT_CHECK_EQ(std::string(arg.values[1]), std::string("blue"));
LT_END_AUTO_TEST(build_multi_args)

// Test querystring
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_querystring)
    auto req = create_test_request()
        .querystring("?foo=bar&baz=qux")
        .build();
    LT_CHECK_EQ(std::string(req.get_querystring()), std::string("?foo=bar&baz=qux"));
LT_END_AUTO_TEST(build_querystring)

// Test content
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_content)
    auto req = create_test_request()
        .content("{\"key\":\"value\"}")
        .build();
    LT_CHECK_EQ(std::string(req.get_content()), std::string("{\"key\":\"value\"}"));
LT_END_AUTO_TEST(build_content)

#ifdef HAVE_BAUTH
// Test basic auth
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_basic_auth)
    auto req = create_test_request()
        .user("admin")
        .pass("secret")
        .build();
    LT_CHECK_EQ(std::string(req.get_user()), std::string("admin"));
    LT_CHECK_EQ(std::string(req.get_pass()), std::string("secret"));
LT_END_AUTO_TEST(build_basic_auth)
#endif  // HAVE_BAUTH

// Test requestor
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_requestor)
    auto req = create_test_request()
        .requestor("192.168.1.1")
        .requestor_port(8080)
        .build();
    LT_CHECK_EQ(std::string(req.get_requestor()), std::string("192.168.1.1"));
    LT_CHECK_EQ(req.get_requestor_port(), static_cast<uint16_t>(8080));
LT_END_AUTO_TEST(build_requestor)

// Test default requestor
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_default_requestor)
    auto req = create_test_request().build();
    LT_CHECK_EQ(std::string(req.get_requestor()), std::string("127.0.0.1"));
    LT_CHECK_EQ(req.get_requestor_port(), static_cast<uint16_t>(0));
LT_END_AUTO_TEST(build_default_requestor)

#ifdef HAVE_GNUTLS
// Test TLS enabled flag
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_tls_enabled)
    auto req = create_test_request()
        .tls_enabled()
        .build();
    LT_CHECK_EQ(req.has_tls_session(), true);
LT_END_AUTO_TEST(build_tls_enabled)

// Test TLS not enabled by default
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_no_tls)
    auto req = create_test_request().build();
    LT_CHECK_EQ(req.has_tls_session(), false);
LT_END_AUTO_TEST(build_no_tls)
#endif  // HAVE_GNUTLS

// Test that all getters on a minimal request return empty without crashing
LT_BEGIN_AUTO_TEST(create_test_request_suite, empty_getters_no_crash)
    auto req = create_test_request().build();
    // These should all return empty/default without crashing
    LT_CHECK_EQ(std::string(req.get_header("Anything")), std::string(""));
    LT_CHECK_EQ(std::string(req.get_footer("Anything")), std::string(""));
    LT_CHECK_EQ(std::string(req.get_cookie("Anything")), std::string(""));
    LT_CHECK_EQ(std::string(req.get_arg_flat("Anything")), std::string(""));
    LT_CHECK_EQ(std::string(req.get_querystring()), std::string(""));
    LT_CHECK_EQ(std::string(req.get_content()), std::string(""));
    LT_CHECK_EQ(req.get_headers().size(), static_cast<size_t>(0));
    LT_CHECK_EQ(req.get_footers().size(), static_cast<size_t>(0));
    LT_CHECK_EQ(req.get_cookies().size(), static_cast<size_t>(0));
    LT_CHECK_EQ(req.get_args().size(), static_cast<size_t>(0));
    LT_CHECK_EQ(req.get_args_flat().size(), static_cast<size_t>(0));
    LT_CHECK_EQ(req.get_path_pieces().size(), static_cast<size_t>(0));
LT_END_AUTO_TEST(empty_getters_no_crash)

// End-to-end: build request, call render, inspect response
class greeting_resource : public http_resource {
 public:
    std::shared_ptr<http_response> render_GET(const http_request& req) override {
        std::string name(req.get_arg_flat("name"));
        if (name.empty()) name = "World";
        return std::make_shared<http_response>(http_response::string("Hello, " + name));
    }
};

LT_BEGIN_AUTO_TEST(create_test_request_suite, render_with_test_request)
    greeting_resource resource;
    auto req = create_test_request()
        .path("/greet")
        .arg("name", "Alice")
        .build();
    auto resp = resource.render_GET(req);
    LT_ASSERT(resp != nullptr);
    // Verify the response body kind is string.
    LT_CHECK_EQ(static_cast<int>(resp->kind()),
                static_cast<int>(httpserver::body_kind::string));
    // Verify the response body content reflects the arg.
    auto* sb = dynamic_cast<httpserver::detail::string_body*>(SBO::body_ptr(*resp));
    LT_ASSERT(sb != nullptr);
    LT_CHECK_EQ(sb->get_content(), std::string("Hello, Alice"));
LT_END_AUTO_TEST(render_with_test_request)

// Test full chain of all builder methods
LT_BEGIN_AUTO_TEST(create_test_request_suite, full_chain)
    auto req = create_test_request()
        .method("PUT")
        .path("/api/resource/42")
        .version("HTTP/1.0")
        .content("request body")
        .header("Content-Type", "text/plain")
        .header("Authorization", "Bearer token123")
        .footer("Trailer", "checksum")
        .cookie("session", "abc")
        .arg("key1", "val1")
        .arg("key2", "val2")
        .querystring("?key1=val1&key2=val2")
#ifdef HAVE_BAUTH
        .user("testuser")
        .pass("testpass")
#endif
        .requestor("10.0.0.1")
        .requestor_port(9090)
        .build();

    LT_CHECK_EQ(std::string(req.get_method()), std::string("PUT"));
    LT_CHECK_EQ(std::string(req.get_path()), std::string("/api/resource/42"));
    LT_CHECK_EQ(std::string(req.get_version()), std::string("HTTP/1.0"));
    LT_CHECK_EQ(std::string(req.get_content()), std::string("request body"));
    LT_CHECK_EQ(std::string(req.get_header("Content-Type")), std::string("text/plain"));
    LT_CHECK_EQ(std::string(req.get_header("Authorization")), std::string("Bearer token123"));
    LT_CHECK_EQ(std::string(req.get_footer("Trailer")), std::string("checksum"));
    LT_CHECK_EQ(std::string(req.get_cookie("session")), std::string("abc"));
    LT_CHECK_EQ(std::string(req.get_arg_flat("key1")), std::string("val1"));
    LT_CHECK_EQ(std::string(req.get_arg_flat("key2")), std::string("val2"));
    LT_CHECK_EQ(std::string(req.get_querystring()), std::string("?key1=val1&key2=val2"));
#ifdef HAVE_BAUTH
    LT_CHECK_EQ(std::string(req.get_user()), std::string("testuser"));
    LT_CHECK_EQ(std::string(req.get_pass()), std::string("testpass"));
#endif
    LT_CHECK_EQ(std::string(req.get_requestor()), std::string("10.0.0.1"));
    LT_CHECK_EQ(req.get_requestor_port(), static_cast<uint16_t>(9090));
LT_END_AUTO_TEST(full_chain)

// Test path pieces work with test request
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_path_pieces)
    auto req = create_test_request()
        .path("/api/users/42")
        .build();
    // TASK-017: get_path_pieces() returns const&; bind by reference.
    const auto& pieces = req.get_path_pieces();
    LT_CHECK_EQ(pieces.size(), static_cast<size_t>(3));
    LT_CHECK_EQ(pieces[0], std::string("api"));
    LT_CHECK_EQ(pieces[1], std::string("users"));
    LT_CHECK_EQ(pieces[2], std::string("42"));
LT_END_AUTO_TEST(build_path_pieces)

// Test method is uppercased
LT_BEGIN_AUTO_TEST(create_test_request_suite, method_uppercase)
    auto req = create_test_request()
        .method("post")
        .build();
    LT_CHECK_EQ(std::string(req.get_method()), std::string("POST"));
LT_END_AUTO_TEST(method_uppercase)

// TASK-017: container getters return `const ContainerType&` aliasing
// impl-owned storage. Repeated calls on the same const http_request must
// return the same address (the cached container in the impl), proving:
//   (a) the return type is a reference (you can take its address),
//   (b) the cache is built once and reused on subsequent calls.
LT_BEGIN_AUTO_TEST(create_test_request_suite, getters_return_const_ref_stable)
    auto req = create_test_request()
        .header("X-Foo", "1")
        .footer("X-Bar", "2")
        .cookie("session", "3")
        .arg("a", "b")
        .path("/p/q/r")
        .build();
    const httpserver::http_request& cref = req;

    // Call each getter twice and verify the address is stable.
    LT_CHECK_EQ(&cref.get_headers(),     &cref.get_headers());
    LT_CHECK_EQ(&cref.get_footers(),     &cref.get_footers());
    LT_CHECK_EQ(&cref.get_cookies(),     &cref.get_cookies());
    LT_CHECK_EQ(&cref.get_args(),        &cref.get_args());
    LT_CHECK_EQ(&cref.get_path_pieces(), &cref.get_path_pieces());
    LT_CHECK_EQ(&cref.get_files(),       &cref.get_files());

    // Sanity: the cached values are also non-empty / correct.
    LT_CHECK_EQ(cref.get_headers().size(), static_cast<size_t>(1));
    LT_CHECK_EQ(cref.get_footers().size(), static_cast<size_t>(1));
    LT_CHECK_EQ(cref.get_cookies().size(), static_cast<size_t>(1));
    LT_CHECK_EQ(cref.get_args().size(),    static_cast<size_t>(1));
    LT_CHECK_EQ(cref.get_path_pieces().size(), static_cast<size_t>(3));
LT_END_AUTO_TEST(getters_return_const_ref_stable)

// TASK-018: per-key getters must be empty-on-miss and must NOT insert
// the missing key into the underlying maps. We assert this externally by
// observing the public container-getter sizes before and after a series
// of misses. The container caches are built on the first call (so we
// snapshot the baseline AFTER the first call), then we hammer the per-key
// getters with missing keys and confirm the container sizes haven't grown.
LT_BEGIN_AUTO_TEST(create_test_request_suite, missing_key_does_not_insert)
    auto req = create_test_request()
        .header("Present", "yes")
        .footer("AlsoPresent", "yes")
        .cookie("CookiePresent", "yes")
        .arg("argKey", "v")
        .build();
    const httpserver::http_request& r = req;

    // Build the container caches once so the size snapshot is stable.
    const auto headers_before = r.get_headers().size();
    const auto footers_before = r.get_footers().size();
    const auto cookies_before = r.get_cookies().size();
    const auto args_before    = r.get_args().size();

    // Five misses on each kind. Each must return empty and must NOT
    // insert into the underlying maps.
    for (int i = 0; i < 5; ++i) {
        LT_CHECK(r.get_header("Missing-Header").empty());
        LT_CHECK(r.get_footer("Missing-Footer").empty());
        LT_CHECK(r.get_cookie("Missing-Cookie").empty());
        LT_CHECK(r.get_arg_flat("Missing-Arg").empty());
        LT_CHECK(r.get_arg("Missing-Arg").values.empty());
    }

    // The container caches expose the underlying map sizes. If any of
    // the per-key misses had inserted, these would have grown.
    LT_CHECK_EQ(r.get_headers().size(), headers_before);
    LT_CHECK_EQ(r.get_footers().size(), footers_before);
    LT_CHECK_EQ(r.get_cookies().size(), cookies_before);
    LT_CHECK_EQ(r.get_args().size(),    args_before);
LT_END_AUTO_TEST(missing_key_does_not_insert)

// TASK-018: per-key getters return string_view aliasing the request's
// owned storage and surface the correct value on a hit.
LT_BEGIN_AUTO_TEST(create_test_request_suite, getters_return_string_view_correct_value)
    auto req = create_test_request()
        .header("X-Foo", "foo-value")
        .footer("X-Bar", "bar-value")
        .cookie("session", "sess-value")
        .arg("q", "q-value")
        .build();
    const httpserver::http_request& r = req;

    LT_CHECK_EQ(std::string(r.get_header("X-Foo")),   std::string("foo-value"));
    LT_CHECK_EQ(std::string(r.get_footer("X-Bar")),   std::string("bar-value"));
    LT_CHECK_EQ(std::string(r.get_cookie("session")), std::string("sess-value"));
    LT_CHECK_EQ(std::string(r.get_arg_flat("q")),     std::string("q-value"));
LT_END_AUTO_TEST(getters_return_string_view_correct_value)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
