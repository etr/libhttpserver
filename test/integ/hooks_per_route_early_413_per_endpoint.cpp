/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-051 acceptance criterion 2:
//
// "New integ test hooks_per_route_early_413_per_endpoint: per-route
//  before_handler hook on /upload-small rejects bodies > 1KB with 413;
//  on /upload-large rejects > 1GB. Different policies, no global
//  side-effects."
//
// Demonstrates per-route policy isolation: the same hook phase
// (before_handler) on two different resources can carry different
// rejection thresholds without one bleeding into the other.

#include <curl/curl.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::before_handler_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8244

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class echo_resource : public http_resource {
 public:
    http_response render_post(const http_request&) override {
        return http_response::string("uploaded");
    }
};

// Helper: POST `body_size` bytes to `path` and return the http status.
long post_bytes(int port, const std::string& path, std::size_t body_size) {  // NOLINT(runtime/int)
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
    std::string body(body_size, 'x');
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body_size));  // NOLINT(runtime/int)
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    long status = 0;  // NOLINT(runtime/int)
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_easy_cleanup(curl);
    return status;
}

}  // namespace

LT_BEGIN_SUITE(hooks_per_route_early_413_per_endpoint_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_per_route_early_413_per_endpoint_suite)

LT_BEGIN_AUTO_TEST(hooks_per_route_early_413_per_endpoint_suite,
                   per_route_413_caps_are_isolated)
    webserver ws{create_webserver(PORT)};

    auto small_r = std::make_shared<echo_resource>();
    auto large_r = std::make_shared<echo_resource>();

    // Per-route before_handler on /upload-small: reject > 1024 bytes.
    auto small_h = small_r->add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [](before_handler_ctx& ctx) -> hook_action {
                if (ctx.request == nullptr) return hook_action{};
                auto cl = ctx.request->get_header("Content-Length");
                if (cl.empty()) return hook_action{};
                std::size_t n = 0;
                try {
                    n = std::stoul(std::string(cl));
                } catch (...) {}
                if (n > 1024) {
                    auto r = http_response::string("too big (small endpoint)");
                    r.with_status(413);
                    return hook_action::respond_with(std::move(r));
                }
                return hook_action{};
            }));

    // Per-route before_handler on /upload-large: reject > 1 GB.
    auto large_h = large_r->add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [](before_handler_ctx& ctx) -> hook_action {
                if (ctx.request == nullptr) return hook_action{};
                auto cl = ctx.request->get_header("Content-Length");
                if (cl.empty()) return hook_action{};
                std::size_t n = 0;
                try {
                    n = std::stoul(std::string(cl));
                } catch (...) {}
                if (n > static_cast<std::size_t>(1024) * 1024 * 1024) {
                    auto r = http_response::string("too big (large endpoint)");
                    r.with_status(413);
                    return hook_action::respond_with(std::move(r));
                }
                return hook_action{};
            }));

    ws.register_path("/upload-small", small_r);
    ws.register_path("/upload-large", large_r);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 2 KB POST to /upload-small -> 413 (over the 1 KB cap).
    long s1 = post_bytes(PORT, "/upload-small", 2048);  // NOLINT(runtime/int)
    LT_CHECK_EQ(s1, 413L);

    // 2 KB POST to /upload-large -> 200 (well under 1 GB).
    long s2 = post_bytes(PORT, "/upload-large", 2048);  // NOLINT(runtime/int)
    LT_CHECK_EQ(s2, 200L);

    // 500 byte POST to /upload-small -> 200 (well under 1 KB).
    long s3 = post_bytes(PORT, "/upload-small", 500);  // NOLINT(runtime/int)
    LT_CHECK_EQ(s3, 200L);

    ws.stop();
LT_END_AUTO_TEST(per_route_413_caps_are_isolated)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
