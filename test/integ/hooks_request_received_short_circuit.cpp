/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-047 acceptance criterion 1 (closes #273).
//
// "A `request_received` hook returning hook_action::respond_with(413) on
//  Content-Length > 1 MB aborts the upload before any body bytes are
//  consumed. Verified by registering a downstream `body_chunk` hook that
//  asserts it was never called."
//
// We start a webserver with a resource at /upload, register a
// `request_received` hook that rejects oversized uploads with 413, and
// a `body_chunk` hook that counts firings. The first sub-test issues a
// 2 MB POST and asserts: HTTP 413, handler never ran, body_chunk hook
// never fired. The second sub-test issues a 100-byte POST and asserts:
// HTTP 200, handler ran, body_chunk hook fired at least once.

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

using httpserver::body_chunk_ctx;
using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::request_received_ctx;
using httpserver::webserver;

#define PORT 8200

namespace {

constexpr std::size_t kCap = 1 * 1024 * 1024;   // 1 MB

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

struct counters {
    std::atomic<std::size_t> body_chunks_observed{0};
    std::atomic<bool> handler_called{false};
};

class upload_resource : public httpserver::http_resource {
 public:
    explicit upload_resource(counters* c) : c_(c) {}
    http_response render_post(const http_request&) override {
        c_->handler_called.store(true, std::memory_order_relaxed);
        return http_response::string("UPLOAD OK");
    }
 private:
    counters* c_;
};

}  // namespace

LT_BEGIN_SUITE(hooks_request_received_short_circuit_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_request_received_short_circuit_suite)

LT_BEGIN_AUTO_TEST(hooks_request_received_short_circuit_suite,
                   oversize_body_short_circuits_before_body_read)
    counters c;
    webserver ws{create_webserver(PORT)};

    auto h_recv = ws.add_hook(hook_phase::request_received,
        std::function<hook_action(request_received_ctx&)>(
            [](request_received_ctx& ctx) {
                std::string cl{ctx.request->get_header("Content-Length")};
                if (cl.empty()) return hook_action::pass();
                try {
                    if (std::stoull(cl) > kCap) {
                        return hook_action::respond_with(
                            http_response::empty().with_status(413));
                    }
                } catch (...) {
                    // malformed Content-Length: let normal pipeline 400.
                }
                return hook_action::pass();
            }));

    auto h_body = ws.add_hook(hook_phase::body_chunk,
        std::function<hook_action(body_chunk_ctx&)>(
            [&c](body_chunk_ctx&) {
                c.body_chunks_observed.fetch_add(1, std::memory_order_relaxed);
                return hook_action::pass();
            }));

    auto resource = std::make_shared<upload_resource>(&c);
    ws.register_path("/upload", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 2 MB body — over the 1 MB cap, must be 413.
    std::string body(2 * 1024 * 1024, 'X');

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/upload";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(http_code, 413L);
    LT_CHECK_EQ(c.handler_called.load(), false);
    LT_CHECK_EQ(c.body_chunks_observed.load(), static_cast<std::size_t>(0));

    ws.stop();
LT_END_AUTO_TEST(oversize_body_short_circuits_before_body_read)

LT_BEGIN_AUTO_TEST(hooks_request_received_short_circuit_suite,
                   small_body_passes_through)
    counters c;
    webserver ws{create_webserver(PORT)};

    auto h_recv = ws.add_hook(hook_phase::request_received,
        std::function<hook_action(request_received_ctx&)>(
            [](request_received_ctx& ctx) {
                std::string cl{ctx.request->get_header("Content-Length")};
                if (cl.empty()) return hook_action::pass();
                try {
                    if (std::stoull(cl) > kCap) {
                        return hook_action::respond_with(
                            http_response::empty().with_status(413));
                    }
                } catch (...) {}
                return hook_action::pass();
            }));

    auto h_body = ws.add_hook(hook_phase::body_chunk,
        std::function<hook_action(body_chunk_ctx&)>(
            [&c](body_chunk_ctx&) {
                c.body_chunks_observed.fetch_add(1, std::memory_order_relaxed);
                return hook_action::pass();
            }));

    auto resource = std::make_shared<upload_resource>(&c);
    ws.register_path("/upload", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string body(100, 'a');

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/upload";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(http_code, 200L);
    LT_CHECK_EQ(c.handler_called.load(), true);
    LT_CHECK(c.body_chunks_observed.load() >= static_cast<std::size_t>(1));

    ws.stop();
LT_END_AUTO_TEST(small_body_passes_through)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
