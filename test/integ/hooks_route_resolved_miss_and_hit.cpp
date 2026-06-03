/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-048 acceptance criterion 1.
//
// "New integ test hooks_route_resolved_miss_and_hit: registers two
//  route_resolved hooks; on a hit, both observe matched != std::nullopt;
//  on a miss, both observe matched == std::nullopt."
//
// We start a webserver with a resource at /hit, register two
// route_resolved hooks that record their per-firing matched-presence
// state into atomic flags, then drive two curl round-trips (GET /hit and
// GET /miss) and inspect the recorded states.

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

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::route_resolved_ctx;
using httpserver::webserver;

#define PORT 8203

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

struct probe_state {
    std::atomic<std::size_t> calls{0};
    std::atomic<std::size_t> hits{0};
    std::atomic<std::size_t> misses{0};
    std::atomic<bool> last_is_prefix{false};
    std::atomic<std::size_t> last_path_len{0};
    // TASK-048 review finding 16: route_resolved_ctx::resource is newly-
    // added public API — pin it with a positive regression assertion.
    // For a route hit, resource should be non-null (the registered
    // http_resource*); for a miss, resource should be null.
    std::atomic<bool> last_hit_resource_non_null{false};
    std::atomic<bool> last_miss_resource_null{true};
};

class hello_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("OK");
    }
};

CURLcode hit_url(const std::string& path, long* http_code) {  // NOLINT(runtime/int)
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return CURLE_FAILED_INIT;
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode rc = curl_easy_perform(curl);
    if (http_code != nullptr) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code);
    }
    curl_easy_cleanup(curl);
    return rc;
}

}  // namespace

LT_BEGIN_SUITE(hooks_route_resolved_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_route_resolved_suite)

LT_BEGIN_AUTO_TEST(hooks_route_resolved_suite, two_hooks_observe_hit_and_miss)
    probe_state a;
    probe_state b;

    webserver ws{create_webserver(PORT)};

    auto h1 = ws.add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [&a](const route_resolved_ctx& ctx) {
                a.calls.fetch_add(1, std::memory_order_relaxed);
                if (ctx.matched.has_value()) {
                    a.hits.fetch_add(1, std::memory_order_relaxed);
                    a.last_is_prefix.store(ctx.matched->is_prefix,
                                           std::memory_order_relaxed);
                    a.last_path_len.store(ctx.matched->path_template.size(),
                                          std::memory_order_relaxed);
                    // TASK-048 review finding 16: record whether resource
                    // is non-null on a hit (should be the registered resource).
                    a.last_hit_resource_non_null.store(ctx.resource != nullptr,
                                                       std::memory_order_relaxed);
                } else {
                    a.misses.fetch_add(1, std::memory_order_relaxed);
                    // On a miss, resource should be null.
                    a.last_miss_resource_null.store(ctx.resource == nullptr,
                                                    std::memory_order_relaxed);
                }
            }));

    auto h2 = ws.add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [&b](const route_resolved_ctx& ctx) {
                b.calls.fetch_add(1, std::memory_order_relaxed);
                if (ctx.matched.has_value()) {
                    b.hits.fetch_add(1, std::memory_order_relaxed);
                } else {
                    b.misses.fetch_add(1, std::memory_order_relaxed);
                }
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hit", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    long code_hit = 0;  // NOLINT(runtime/int)
    long code_miss = 0;  // NOLINT(runtime/int)
    LT_CHECK_EQ(hit_url("/hit", &code_hit), CURLE_OK);
    LT_CHECK_EQ(hit_url("/miss", &code_miss), CURLE_OK);
    LT_CHECK_EQ(code_hit, 200L);
    LT_CHECK_EQ(code_miss, 404L);

    ws.stop();

    LT_CHECK_EQ(a.calls.load(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(a.hits.load(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(a.misses.load(), static_cast<std::size_t>(1));

    LT_CHECK_EQ(b.calls.load(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(b.hits.load(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(b.misses.load(), static_cast<std::size_t>(1));

    // Hit's path_template must be non-empty and is_prefix==false for
    // exact-path registrations.
    LT_CHECK(a.last_path_len.load() >= static_cast<std::size_t>(1));
    LT_CHECK_EQ(a.last_is_prefix.load(), false);

    // TASK-048 review finding 16: route_resolved_ctx::resource is newly-
    // added API. Assert it is non-null for a hit (the registered resource
    // pointer) and null for a miss (no matched route).
    LT_CHECK(a.last_hit_resource_non_null.load());
    LT_CHECK(a.last_miss_resource_null.load());
LT_END_AUTO_TEST(two_hooks_observe_hit_and_miss)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
