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

#include <stdexcept>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::auth_handler_ptr;
using httpserver::create_webserver;
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
// Per-test offsets are paired so the curl URL cannot silently drift from
// the create_webserver() port if PORT is ever changed.
#define PORT_1 (PORT + 1)
#define PORT_1_STRING "8291"
#define PORT_2 (PORT + 2)
#define PORT_2_STRING "8292"
#define PORT_3 (PORT + 3)
#define PORT_3_STRING "8293"
#define PORT_4 (PORT + 4)
#define PORT_4_STRING "8294"

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

}  // namespace

LT_BEGIN_SUITE(auth_handler_optional_signature_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(auth_handler_optional_signature_suite)

// The hook-count contract for this setter lives in hooks_alias_count_test.cpp
// (auth_handler_registers_one_before_handler). This TU focuses on compile-time
// type pinning and end-to-end wire behaviour.

// 1. Lambda returning std::nullopt -> 200, resource body served.
LT_BEGIN_AUTO_TEST(auth_handler_optional_signature_suite,
                   nullopt_allows_request_through)
    webserver ws{create_webserver(PORT_1)
        .auth_handler([](const http_request&)
                          -> std::optional<http_response> {
            return std::nullopt;
        })};
    simple_resource sr;
    ws.register_path("/protected", std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:" PORT_1_STRING "/protected");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("SUCCESS"));

    ws.stop();
LT_END_AUTO_TEST(nullopt_allows_request_through)

// 2. Lambda returning an engaged optional with status 401 short-circuits
//    dispatch and the body comes from the auth_handler.
LT_BEGIN_AUTO_TEST(auth_handler_optional_signature_suite,
                   engaged_optional_rejects_request)
    webserver ws{create_webserver(PORT_2)
        .auth_handler([](const http_request&)
                          -> std::optional<http_response> {
            return http_response::string("blocked").with_status(401);
        })};
    simple_resource sr;
    ws.register_path("/protected", std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:" PORT_2_STRING "/protected");
    LT_CHECK_EQ(fr.response_code, 401);
    LT_CHECK_EQ(fr.body, std::string("blocked"));

    ws.stop();
LT_END_AUTO_TEST(engaged_optional_rejects_request)

// 3. Auth handler throwing a std::runtime_error: pin the observable
//    contract for error paths through the hook-invocation machinery.
//    The current dispatch path swallows the exception and lets the
//    request progress (equivalent to nullopt). The hook framework's
//    handler-exception slot is the named extension point for converting
//    an exception into a custom response; the alias hook itself stays
//    non-fatal so a buggy auth callable cannot DoS the whole server.
//    A future change that short-circuits with 500 must update this pin.
LT_BEGIN_AUTO_TEST(auth_handler_optional_signature_suite,
                   throwing_handler_is_swallowed_and_request_passes)
    webserver ws{create_webserver(PORT_3)
        .auth_handler([](const http_request&)
                          -> std::optional<http_response> {
            throw std::runtime_error("auth handler exploded");
        })};
    simple_resource sr;
    ws.register_path("/protected", std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:" PORT_3_STRING "/protected");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("SUCCESS"));

    ws.stop();
LT_END_AUTO_TEST(throwing_handler_is_swallowed_and_request_passes)

// 4. Engaged optional carrying a 64 KB body arrives uncorrupted on the
//    wire. Regression target for the MOVE from the optional into
//    hook_action::respond_with: the SBO claim in the production
//    comments would be falsified if a large payload were truncated.
LT_BEGIN_AUTO_TEST(auth_handler_optional_signature_suite,
                   large_engaged_optional_payload_arrives_intact)
    constexpr std::size_t kBodySize = 64 * 1024;
    const std::string large_body(kBodySize, 'A');
    webserver ws{create_webserver(PORT_4)
        .auth_handler([large_body](const http_request&)
                          -> std::optional<http_response> {
            return http_response::string(large_body).with_status(403);
        })};
    simple_resource sr;
    ws.register_path("/protected", std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:" PORT_4_STRING "/protected");
    LT_CHECK_EQ(fr.response_code, 403);
    LT_CHECK_EQ(fr.body.size(), kBodySize);
    LT_CHECK(fr.body == large_body);

    ws.stop();
LT_END_AUTO_TEST(large_engaged_optional_payload_arrives_intact)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
