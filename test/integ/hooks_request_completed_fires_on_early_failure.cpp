/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-050 acceptance criterion 4.
//
// "A request_received hook short-circuits to a 413; request_completed
//  still fires with succeeded == true and the 413 response object
//  visible."
//
// Pins: (1) request_completed fires unconditionally, (2) the
// short-circuited 413 is visible via ctx.resp, (3) succeeded is true
// because MHD drove the request to ordinary completion (the rejection
// was at the libhttpserver layer, not in the MHD transport).

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::request_completed_ctx;
using httpserver::request_received_ctx;
using httpserver::webserver;

#define PORT 8243

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class never_reached_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("UNREACHED").with_status(200);
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_request_completed_fires_on_early_failure_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_request_completed_fires_on_early_failure_suite)

LT_BEGIN_AUTO_TEST(hooks_request_completed_fires_on_early_failure_suite,
                   short_circuited_413_still_completes)
    std::atomic<std::size_t> rc_calls{0};
    std::atomic<bool> rc_succeeded{false};
    std::atomic<int> rc_status{-1};
    std::atomic<bool> rc_resp_non_null{false};

    webserver ws{create_webserver(PORT)};

    // Short-circuit at request_received with a 413.
    auto h_rr = ws.add_hook(hook_phase::request_received,
        std::function<hook_action(request_received_ctx&)>(
            [](request_received_ctx&) {
                return hook_action::respond_with(
                    http_response::string("REJECTED").with_status(413));
            }));

    auto h_rc = ws.add_hook(hook_phase::request_completed,
        std::function<void(const request_completed_ctx&)>(
            [&](const request_completed_ctx& ctx) {
                rc_calls.fetch_add(1, std::memory_order_relaxed);
                rc_succeeded.store(ctx.succeeded,
                                   std::memory_order_relaxed);
                rc_resp_non_null.store(ctx.resp != nullptr,
                                       std::memory_order_relaxed);
                if (ctx.resp != nullptr) {
                    rc_status.store(ctx.resp->get_status(),
                                    std::memory_order_relaxed);
                }
            }));

    auto resource = std::make_shared<never_reached_resource>();
    ws.register_path("/anything", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/anything";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;  // NOLINT(runtime/int) -- libcurl API takes long*
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    // ws.stop() drains all pending MHD worker-thread callbacks (including
    // request_completed) before returning, so no post-stop sleep is needed.
    ws.stop();

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(code, 413L);
    LT_CHECK_EQ(body, std::string("REJECTED"));
    LT_CHECK_EQ(rc_calls.load(), static_cast<std::size_t>(1));
    LT_CHECK(rc_succeeded.load());
    LT_CHECK(rc_resp_non_null.load());
    LT_CHECK_EQ(rc_status.load(), 413);
LT_END_AUTO_TEST(short_circuited_413_still_completes)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
