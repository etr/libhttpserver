/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-048 acceptance criterion 2.
//
// "New integ test hooks_before_handler_short_circuit_replaces_dispatch: a
//  before_handler hook returning hook_action::respond_with(r) is observed
//  on the wire; the resource's render_get is never called."
//
// We register a resource at /secure whose render_get increments a
// counter, install a before_handler hook that returns respond_with(403,
// "BLOCKED"), and issue GET /secure. We assert: HTTP 403, body
// "BLOCKED", counter unchanged.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::before_handler_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_method;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8204

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class secure_resource : public httpserver::http_resource {
 public:
    explicit secure_resource(std::atomic<std::size_t>* c) : c_(c) {}
    http_response render_get(const http_request&) override {
        c_->fetch_add(1, std::memory_order_relaxed);
        return http_response::string("PRIVATE-DATA");
    }
 private:
    std::atomic<std::size_t>* c_;
};

}  // namespace

LT_BEGIN_SUITE(hooks_before_handler_short_circuit_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_before_handler_short_circuit_suite)

LT_BEGIN_AUTO_TEST(hooks_before_handler_short_circuit_suite,
                   short_circuit_skips_resource_handler)
    std::atomic<std::size_t> handler_calls{0};
    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [](before_handler_ctx&) {
                return hook_action::respond_with(
                    http_response::string("BLOCKED").with_status(403));
            }));

    auto resource = std::make_shared<secure_resource>(&handler_calls);
    ws.register_path("/secure", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/secure";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    ws.stop();

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(http_code, 403L);
    LT_CHECK_EQ(resp_body, std::string("BLOCKED"));
    LT_CHECK_EQ(handler_calls.load(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(short_circuit_skips_resource_handler)

LT_BEGIN_AUTO_TEST(hooks_before_handler_short_circuit_suite,
                   pass_invokes_resource_handler)
    std::atomic<std::size_t> handler_calls{0};
    // Capture ctx fields to verify the firing site populates them correctly.
    // Stored as uint8_t (underlying type of http_method) and bool for
    // atomic compatibility.
    std::atomic<std::uint8_t> ctx_method{
        static_cast<std::uint8_t>(http_method::count_)};  // sentinel: "not set"
    std::atomic<bool> ctx_resource_non_null{false};
    webserver ws{create_webserver(PORT)};

    auto h = ws.add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [&ctx_method, &ctx_resource_non_null](before_handler_ctx& ctx) {
                ctx_method.store(static_cast<std::uint8_t>(ctx.method),
                                 std::memory_order_relaxed);
                ctx_resource_non_null.store(ctx.resource != nullptr,
                                            std::memory_order_relaxed);
                return hook_action::pass();
            }));

    auto resource = std::make_shared<secure_resource>(&handler_calls);
    ws.register_path("/secure", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/secure";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    ws.stop();

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(http_code, 200L);
    LT_CHECK_EQ(resp_body, std::string("PRIVATE-DATA"));
    LT_CHECK_EQ(handler_calls.load(), static_cast<std::size_t>(1));
    // Verify the firing site populates ctx.method and ctx.resource correctly
    // for a GET request on a registered route (TASK-048 review finding 14).
    LT_CHECK_EQ(ctx_method.load(),
                static_cast<std::uint8_t>(http_method::get));
    LT_CHECK(ctx_resource_non_null.load());
LT_END_AUTO_TEST(pass_invokes_resource_handler)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
