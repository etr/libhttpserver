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

// TASK-053: v2 dispatch contract gate.
//
// This TU pins the *end-to-end* observable invariants the dispatch path
// must satisfy through the public webserver surface, BEFORE we cut over
// finalize_answer() from resolve_resource_for_request (v1) to
// resolve_resource_for_request_v2 (lookup_v2-backed). Each test fires a
// real HTTP request and asserts on response body / status / hook context.
//
// The four pinned invariants:
//   1. Parameterized routes: `/users/{id}` matched against `/users/42`
//      populates `req.get_arg("id") == "42"`.
//   2. Prefix routes: `/static` matched against `/static/foo/bar` hits
//      the registered resource and `ctx.matched->is_prefix == true`.
//   3. Exact routes: `/exact` returns `ctx.matched->is_prefix == false`.
//   4. Method mismatch: POST to a GET-only route still returns 405 and
//      the route_resolved hook ctx still carries a non-null resource
//      pointer (the resolve step ran; only the method check failed).
//
// All four currently pass against the v1 dispatch path. They MUST keep
// passing after the v2 cutover. Anchoring them HERE — pre-cutover — is
// the "safety net first" pattern that lets each subsequent step land
// without regression risk.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// wait_for_server_ready: poll the given port with an HTTP GET / until the
// server responds (any status), or until the deadline elapses. This avoids
// a fixed sleep that is either too short on slow/sanitizer CI builds or
// wastes 50 ms of wall time on every test invocation.
static void wait_for_server_ready(int port,
                                  std::chrono::milliseconds deadline
                                      = std::chrono::milliseconds(3000)) {
    using clock = std::chrono::steady_clock;
    auto end = clock::now() + deadline;
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    while (clock::now() < end) {
        CURL* c = curl_easy_init();
        if (!c) break;
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 50L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 50L);
        CURLcode rc = curl_easy_perform(c);
        curl_easy_cleanup(c);
        if (rc == CURLE_OK) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::route_resolved_ctx;
using httpserver::webserver;

// 8260 is unused elsewhere in the test/ tree as of TASK-053 validation
// iter2; the prior 8231 collided with
// test/integ/hooks_handler_exception_user_handler_throws_continues_chain.cpp
// and caused intermittent EADDRINUSE under `make check -j`.
#define PORT 8260

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Echo the `id` URL parameter back in the response body so the test
// can read it via the body stream. Used by the parameterized-route test.
class echo_id_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request& req) override {
        return http_response::string(std::string(req.get_arg("id")));
    }
};

class hello_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("OK");
    }
};

// Performs a GET and returns body + status code.
struct response_capture {
    long status = 0;
    std::string body;
};

response_capture do_get(const std::string& path) {
    response_capture out;
    CURL* curl = curl_easy_init();
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    curl_easy_cleanup(curl);
    return out;
}

// Performs a POST with no body and returns the status code.
long do_post_status(const std::string& path) {
    long status = 0;
    CURL* curl = curl_easy_init();
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    std::string sink;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    return status;
}

// Probe state for the route_resolved hook ctx. Captures the most-recent
// invocation's matched flags and resource pointer so we can assert them
// in the test bodies.
struct hook_probe {
    std::atomic<std::size_t> calls{0};
    std::atomic<bool> last_matched_engaged{false};
    std::atomic<bool> last_is_prefix{false};
    std::atomic<bool> last_resource_non_null{false};
};

}  // namespace

LT_BEGIN_SUITE(v2_dispatch_contract_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(v2_dispatch_contract_suite)

// Invariant 1: parameterized route — `/users/{id}` against `/users/42`
// must populate `req.get_arg("id") == "42"` (the v2 equivalent of
// apply_extracted_params).
LT_BEGIN_AUTO_TEST(v2_dispatch_contract_suite, parameterized_route_extracts_capture)
    webserver ws{create_webserver(PORT)};
    auto resource = std::make_shared<echo_id_resource>();
    ws.register_path("/users/{id}", resource);
    ws.start(false);
    wait_for_server_ready(PORT);

    response_capture r = do_get("/users/42");
    ws.stop();

    LT_CHECK_EQ(r.status, 200L);
    LT_CHECK_EQ(r.body, std::string("42"));
LT_END_AUTO_TEST(parameterized_route_extracts_capture)

// Invariant 2: prefix route — `/static` matched against `/static/foo/bar`
// must hit and route_resolved ctx must carry is_prefix=true.
LT_BEGIN_AUTO_TEST(v2_dispatch_contract_suite, prefix_route_marks_is_prefix_true)
    hook_probe probe;
    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [&probe](const route_resolved_ctx& ctx) {
                probe.calls.fetch_add(1, std::memory_order_relaxed);
                if (ctx.matched.has_value()) {
                    probe.last_matched_engaged.store(true,
                                                     std::memory_order_relaxed);
                    probe.last_is_prefix.store(ctx.matched->is_prefix,
                                                std::memory_order_relaxed);
                    probe.last_resource_non_null.store(
                        ctx.resource != nullptr,
                        std::memory_order_relaxed);
                }
            }));
    (void)h;

    auto resource = std::make_shared<hello_resource>();
    ws.register_prefix("/static", resource);
    ws.start(false);
    wait_for_server_ready(PORT);

    response_capture r = do_get("/static/foo/bar");
    ws.stop();

    LT_CHECK_EQ(r.status, 200L);
    LT_CHECK(probe.last_matched_engaged.load());
    LT_CHECK_EQ(probe.last_is_prefix.load(), true);
    LT_CHECK(probe.last_resource_non_null.load());
LT_END_AUTO_TEST(prefix_route_marks_is_prefix_true)

// Invariant 3: exact route — `/exact` hit must carry is_prefix=false.
LT_BEGIN_AUTO_TEST(v2_dispatch_contract_suite, exact_route_marks_is_prefix_false)
    hook_probe probe;
    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [&probe](const route_resolved_ctx& ctx) {
                probe.calls.fetch_add(1, std::memory_order_relaxed);
                if (ctx.matched.has_value()) {
                    probe.last_matched_engaged.store(true,
                                                     std::memory_order_relaxed);
                    probe.last_is_prefix.store(ctx.matched->is_prefix,
                                                std::memory_order_relaxed);
                    probe.last_resource_non_null.store(
                        ctx.resource != nullptr,
                        std::memory_order_relaxed);
                }
            }));
    (void)h;

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/exact", resource);
    ws.start(false);
    wait_for_server_ready(PORT);

    response_capture r = do_get("/exact");
    ws.stop();

    LT_CHECK_EQ(r.status, 200L);
    LT_CHECK(probe.last_matched_engaged.load());
    LT_CHECK_EQ(probe.last_is_prefix.load(), false);
    LT_CHECK(probe.last_resource_non_null.load());
LT_END_AUTO_TEST(exact_route_marks_is_prefix_false)

// Invariant 4: method mismatch returns 405; the route_resolved hook ctx
// MUST still carry a non-null resource pointer because route resolution
// succeeded — the method check that produces 405 runs AFTER the lookup
// and uses the resolved resource's get_allowed_methods().
LT_BEGIN_AUTO_TEST(v2_dispatch_contract_suite, method_mismatch_still_resolves_route)
    hook_probe probe;
    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [&probe](const route_resolved_ctx& ctx) {
                probe.calls.fetch_add(1, std::memory_order_relaxed);
                if (ctx.matched.has_value()) {
                    probe.last_matched_engaged.store(true,
                                                     std::memory_order_relaxed);
                    probe.last_is_prefix.store(ctx.matched->is_prefix,
                                               std::memory_order_relaxed);
                    probe.last_resource_non_null.store(
                        ctx.resource != nullptr,
                        std::memory_order_relaxed);
                }
            }));
    (void)h;

    auto resource = std::make_shared<hello_resource>();
    // Constrain the resource to GET-only so a POST against /get_only
    // exercises the dispatch path's 405 branch — the lookup MUST resolve
    // the resource (so the hook ctx carries it) and the method check
    // that runs AFTER the lookup returns 405.
    resource->disallow_all();
    resource->set_allowing(httpserver::http_method::get, true);
    ws.register_path("/get_only", resource);
    ws.start(false);
    wait_for_server_ready(PORT);

    long post_status = do_post_status("/get_only");
    ws.stop();

    LT_CHECK_EQ(post_status, 405L);
    LT_CHECK(probe.last_matched_engaged.load());
    LT_CHECK(probe.last_resource_non_null.load());
    // /get_only is an exact (non-prefix) route; is_prefix must be false
    // in the 405 code path as well as in the 200 code path.
    LT_CHECK_EQ(probe.last_is_prefix.load(), false);
LT_END_AUTO_TEST(method_mismatch_still_resolves_route)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
