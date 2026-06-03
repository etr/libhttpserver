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

// TASK-054 — one-transitional-build escape hatch.
//
// The v1 auth handler shape (std::function<std::shared_ptr<http_response>(...)>)
// must keep compiling for ONE build cycle so existing call sites get a
// deprecation warning before they break, not a hard build failure.
//
// This TU pins:
//   1. The deprecated typedef alias
//      `httpserver::compat::auth_handler_v1_ptr` exists and is the v1
//      std::shared_ptr<http_response>-returning std::function shape.
//   2. The deprecated overload `create_webserver::auth_handler(
//      compat::auth_handler_v1_ptr)` accepts a v1-shaped callable, and
//      the resulting webserver registers exactly ONE before_handler hook
//      (the auth alias) — proving the shim flows through the same
//      installation path as the canonical optional<http_response> setter.
//   3. End-to-end: a legacy callable returning a null shared_ptr passes
//      the request through (200). A legacy callable returning a non-null
//      shared_ptr whose carried http_response has status 401 plus a
//      custom header gets forwarded VERBATIM (status + header observable
//      on the wire). This confirms `compat::adapt_legacy_auth` MOVES the
//      pointed-to response state into the new optional rather than
//      losing fields.
//
// The TU-wide pragma below suppresses the deprecation warning so this
// test compiles cleanly under -Werror=deprecated-declarations. The
// CONTRACT we are pinning is "warning fires, build does not break"; the
// warning is verified by reviewer inspection of `[[deprecated(...)]]`
// attributes on `compat::auth_handler_v1_ptr` and the setter overload
// (see PR notes / verification checklist for TASK-054).

#include <curl/curl.h>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// File-scope: this TU intentionally exercises [[deprecated]] surface.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

// ---- Compile-time pin: the legacy v1 typedef shape ---------------------

static_assert(std::is_same_v<
                  httpserver::compat::auth_handler_v1_ptr,
                  std::function<std::shared_ptr<http_response>(
                      const http_request&)>>,
              "compat::auth_handler_v1_ptr must preserve the v1 "
              "std::shared_ptr<http_response>-returning std::function "
              "shape for one transitional build (TASK-054).");

// ---- Runtime: legacy shape installs one hook + works on the wire ------

#define PORT 8295
#define PORT_STRING "8295"
// Paired numeric/string forms so the curl URL strings cannot silently
// drift from the constructor port (TASK-054 review #14).
#define PORT_1 (PORT + 1)
#define PORT_1_STRING "8296"
#define PORT_2 (PORT + 2)
#define PORT_2_STRING "8297"

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
    std::string header_x_blocked_by;
};

size_t headerfunc(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* hdr = reinterpret_cast<std::string*>(userdata);
    std::string line(buffer, size * nitems);
    // We capture only X-Blocked-By for the verbatim-forwarding assertion.
    constexpr const char prefix[] = "X-Blocked-By:";
    if (line.compare(0, sizeof(prefix) - 1, prefix) == 0) {
        // Strip the leading "X-Blocked-By: " and trailing CRLF.
        std::string value = line.substr(sizeof(prefix) - 1);
        while (!value.empty() &&
               (value.front() == ' ' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        while (!value.empty() &&
               (value.back() == '\r' || value.back() == '\n')) {
            value.pop_back();
        }
        *hdr = std::move(value);
    }
    return size * nitems;
}

fetch_result fetch(const std::string& url) {
    fetch_result fr{0, {}, {}};
    CURL* curl = curl_easy_init();
    if (!curl) return fr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fr.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &fr.header_x_blocked_by);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &fr.response_code);
    curl_easy_cleanup(curl);
    return fr;
}

}  // namespace

LT_BEGIN_SUITE(auth_handler_legacy_shim_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(auth_handler_legacy_shim_suite)

// The legacy setter's hook-count contract lives in hooks_alias_count_test.cpp
// (legacy_auth_handler_registers_one_before_handler). This TU focuses on
// compile-type pinning and end-to-end wire behaviour of the deprecated shim.

// 2. Legacy callable returning nullptr -> 200 OK on the wire.
LT_BEGIN_AUTO_TEST(auth_handler_legacy_shim_suite,
                   legacy_nullptr_allows_request_through)
    httpserver::compat::auth_handler_v1_ptr legacy =
        [](const http_request&) -> std::shared_ptr<http_response> {
            return nullptr;
        };
    webserver ws{create_webserver(PORT_1)
        .auth_handler(legacy)};
    simple_resource sr;
    ws.register_path("/protected",
                     std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:" PORT_1_STRING "/protected");
    LT_CHECK_EQ(fr.response_code, 200);
    LT_CHECK_EQ(fr.body, std::string("SUCCESS"));

    ws.stop();
LT_END_AUTO_TEST(legacy_nullptr_allows_request_through)

// 3. Legacy callable returning a non-null shared_ptr<http_response> with
//    a non-default status (401) and a custom header is forwarded VERBATIM
//    via compat::adapt_legacy_auth — the v1 response state must end up
//    on the wire intact (status + header), proving the shim moves the
//    payload rather than synthesising a default response.
LT_BEGIN_AUTO_TEST(auth_handler_legacy_shim_suite,
                   legacy_response_state_is_forwarded_verbatim)
    httpserver::compat::auth_handler_v1_ptr legacy =
        [](const http_request&) -> std::shared_ptr<http_response> {
            auto resp = std::make_shared<http_response>(
                http_response::string("blocked").with_status(401));
            resp->with_header("X-Blocked-By", "auth-shim");
            return resp;
        };
    webserver ws{create_webserver(PORT_2)
        .auth_handler(legacy)};
    simple_resource sr;
    ws.register_path("/protected",
                     std::shared_ptr<http_resource>(&sr, [](http_resource*){}));
    ws.start(false);

    fetch_result fr = fetch("localhost:" PORT_2_STRING "/protected");
    LT_CHECK_EQ(fr.response_code, 401);
    LT_CHECK_EQ(fr.body, std::string("blocked"));
    LT_CHECK_EQ(fr.header_x_blocked_by, std::string("auth-shim"));

    ws.stop();
LT_END_AUTO_TEST(legacy_response_state_is_forwarded_verbatim)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
