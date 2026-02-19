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
#include "./littletest.hpp"

using httpserver::create_test_request;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::string_response;
using httpserver::file_response;

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

    auto headers = req.get_headers();
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

    auto footers = req.get_footers();
    LT_CHECK_EQ(footers.size(), static_cast<size_t>(1));

    auto cookies = req.get_cookies();
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
        return std::make_shared<string_response>("Hello, " + name);
    }
};

LT_BEGIN_AUTO_TEST(create_test_request_suite, render_with_test_request)
    greeting_resource resource;
    auto req = create_test_request()
        .path("/greet")
        .arg("name", "Alice")
        .build();
    auto resp = resource.render_GET(req);
    auto* sr = dynamic_cast<string_response*>(resp.get());
    LT_ASSERT(sr != nullptr);
    LT_CHECK_EQ(std::string(sr->get_content()), std::string("Hello, Alice"));
LT_END_AUTO_TEST(render_with_test_request)

// Test string_response get_content
LT_BEGIN_AUTO_TEST(create_test_request_suite, string_response_get_content)
    string_response resp("test body", 200);
    LT_CHECK_EQ(std::string(resp.get_content()), std::string("test body"));
LT_END_AUTO_TEST(string_response_get_content)

// Test file_response get_filename
LT_BEGIN_AUTO_TEST(create_test_request_suite, file_response_get_filename)
    file_response resp("/tmp/test.txt", 200);
    LT_CHECK_EQ(std::string(resp.get_filename()), std::string("/tmp/test.txt"));
LT_END_AUTO_TEST(file_response_get_filename)

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
        .user("testuser")
        .pass("testpass")
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
    LT_CHECK_EQ(std::string(req.get_user()), std::string("testuser"));
    LT_CHECK_EQ(std::string(req.get_pass()), std::string("testpass"));
    LT_CHECK_EQ(std::string(req.get_requestor()), std::string("10.0.0.1"));
    LT_CHECK_EQ(req.get_requestor_port(), static_cast<uint16_t>(9090));
LT_END_AUTO_TEST(full_chain)

// Test path pieces work with test request
LT_BEGIN_AUTO_TEST(create_test_request_suite, build_path_pieces)
    auto req = create_test_request()
        .path("/api/users/42")
        .build();
    auto pieces = req.get_path_pieces();
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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
