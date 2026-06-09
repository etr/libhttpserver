/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-050 acceptance criterion 3.
//
// "A response_sent hook sees status == 200, bytes_queued == body.size(),
//  elapsed > 0."

#include <curl/curl.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./server_ready.hpp"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::response_sent_ctx;
using httpserver::webserver;

#define PORT 8242

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class fixed_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        // 13-byte body. The hook should observe bytes_queued == 13.
        return http_response::string("Hello, World!");
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_response_sent_carries_status_bytes_timing_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_response_sent_carries_status_bytes_timing_suite)

LT_BEGIN_AUTO_TEST(hooks_response_sent_carries_status_bytes_timing_suite,
                   ctx_fields_populated)
    std::atomic<int> seen_status{-1};
    std::atomic<std::size_t> seen_bytes{0};
    std::atomic<int64_t> seen_elapsed_ns{0};
    std::atomic<std::size_t> calls{0};

    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::response_sent,
        std::function<void(const response_sent_ctx&)>(
            [&](const response_sent_ctx& ctx) {
                seen_status.store(ctx.status, std::memory_order_relaxed);
                seen_bytes.store(ctx.bytes_queued,
                                 std::memory_order_relaxed);
                seen_elapsed_ns.store(ctx.elapsed.count(),
                                      std::memory_order_relaxed);
                calls.fetch_add(1, std::memory_order_relaxed);
            }));

    auto resource = std::make_shared<fixed_resource>();
    ws.register_path("/hello", resource);
    ws.start(false);
    httpserver_test::wait_for_server_ready(PORT);

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/hello";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    ws.stop();

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(body, std::string("Hello, World!"));
    LT_CHECK_EQ(calls.load(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(seen_status.load(), 200);
    LT_CHECK_EQ(seen_bytes.load(), static_cast<std::size_t>(13));
    LT_CHECK(seen_elapsed_ns.load() > 0);
LT_END_AUTO_TEST(ctx_fields_populated)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
