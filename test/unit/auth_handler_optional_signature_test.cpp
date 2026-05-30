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

// TASK-054 — sentinel that pins the new auth_handler_ptr typedef shape.
//
// Goal: catch any future drift back to the v1 std::shared_ptr<http_response>
// return type at COMPILE TIME, plus exercise the end-to-end happy-path
// through the auth alias hook with the new optional<http_response> shape:
//
//   - lambda returning std::nullopt              -> 200 OK
//   - lambda returning std::optional{http_response::string(...).with_status(401)}
//                                                -> 401 with that body
//
// The static_assert at the top of this TU is the load-bearing pin: it
// fires at compile time if auth_handler_ptr ever stops being
//   std::function<std::optional<http_response>(const http_request&)>.
//
// The runtime test confirms the dispatch path consumes the optional
// correctly (i.e. webserver_aliases.cpp's auth short-circuit lambda was
// migrated in lockstep with the typedef).

#include <curl/curl.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

using httpserver::auth_handler_ptr;
using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

// ---- Compile-time pin (the load-bearing assertion) ---------------------

static_assert(std::is_same_v<
                  auth_handler_ptr,
                  std::function<std::optional<http_response>(
                      const http_request&)>>,
              "auth_handler_ptr must be "
              "std::function<std::optional<http_response>("
              "const http_request&)> (TASK-054).");

// ---- Runtime contract: end-to-end ---------------------------------------

#define PORT 8290
#define PORT_STRING "8290"

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class simple_resource : public http_resource {
 public:
     http_response render_get(const http_request&) override {
         return http_response::string("SUCCESS");
     }
};

struct fetch_result {
    long response_code;  // NOLINT(runtime/int)
    std::string body;
};

fetch_result fetch(const std::string& url) {
    fetch_result fr{0, {}};
    CURL* curl = curl_easy_init();
    if (!curl) return fr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fr.body);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &fr.response_code);
    curl_easy_cleanup(curl);
    return fr;
}

std::size_t before_handler_count(webserver& ws) {
    auto* impl = httpserver::webserver_test_access::impl(ws);
    return impl->hooks_before_handler_.size();
}

}  // namespace

LT_BEGIN_SUITE(auth_handler_optional_signature_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(auth_handler_optional_signature_suite)

// 1. New-signature lambda is accepted by the auth_handler setter and
//    installs exactly one hook at hook_phase::before_handler.
LT_BEGIN_AUTO_TEST(auth_handler_optional_signature_suite,
                   accepting_optional_lambda_registers_one_before_handler)
    webserver ws{create_webserver(PORT)
        .auth_handler([](const http_request&)
                          -> std::optional<http_response> {
            return std::nullopt;
        })};
    LT_CHECK_EQ(before_handler_count(ws), static_cast<std::size_t>(1));
LT_END_AUTO_TEST(accepting_optional_lambda_registers_one_before_handler)

// 2. Lambda returning std::nullopt -> 200, resource body served.
LT_BEGIN_AUTO_TEST(auth_handler_optional_signature_suite,
                   nullopt_allows_request_through)
    webserver ws{create_webserver(PORT + 1)
        .auth_handler([](const http_request&)
                          -> std::optional<http_response> {
            return std::nullopt;
        })};
    simple_resource sr;
    ws.register_path("/protected", std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:8291/protected");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("SUCCESS"));

    ws.stop();
LT_END_AUTO_TEST(nullopt_allows_request_through)

// 3. Lambda returning an engaged optional with status 401 short-circuits
//    dispatch and the body comes from the auth_handler.
LT_BEGIN_AUTO_TEST(auth_handler_optional_signature_suite,
                   engaged_optional_rejects_request)
    webserver ws{create_webserver(PORT + 2)
        .auth_handler([](const http_request&)
                          -> std::optional<http_response> {
            return std::optional<http_response>(
                http_response::string("blocked").with_status(401));
        })};
    simple_resource sr;
    ws.register_path("/protected", std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:8292/protected");
    LT_CHECK_EQ(fr.response_code, 401);
    LT_CHECK_EQ(fr.body, std::string("blocked"));

    ws.stop();
LT_END_AUTO_TEST(engaged_optional_rejects_request)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
