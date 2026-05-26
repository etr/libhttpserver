/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-049 acceptance criterion 4 / DR-009 §5.2 point 4 backstop.
//
// "The hardcoded empty-body-500 fallback still fires when every hook in
//  the chain (including the internal_error_handler alias) either throws
//  or returns pass()."
//
// Scenario: hook A returns pass(); hook B throws; the
// internal_error_handler alias C itself throws too. Every entry in the
// chain has had its turn and produced nothing -> the dispatcher must
// emit an empty-body 500. The log_error capture should record both A's
// (no), B's (yes) and C's (yes) throws.
//
// Critically, the user internal_error_handler callable must be observed
// EXACTLY ONCE on this request (the alias-slot invocation). The
// dispatcher must NOT re-enter run_internal_error_handler_safely after
// the chain ran, which would call C a second time.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::handler_exception_ctx;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8232

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class throwing_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        throw std::runtime_error("boom");
    }
};

struct log_capture {
    std::mutex m;
    std::string buf;
    void append(const std::string& msg) {
        std::lock_guard<std::mutex> g(m);
        buf.append(msg).append("\n");
    }
    std::string read() {
        std::lock_guard<std::mutex> g(m);
        return buf;
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_handler_exception_fallback_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_handler_exception_fallback_suite)

LT_BEGIN_AUTO_TEST(hooks_handler_exception_fallback_suite,
                   all_hooks_fail_yields_hardcoded_empty_500)
    std::atomic<std::size_t> a_called{0};
    std::atomic<std::size_t> b_called{0};
    std::atomic<std::size_t> c_called{0};
    log_capture cap;
    auto logger = [&cap](const std::string& msg) { cap.append(msg); };

    auto alias_c =
        [&c_called](const http_request&, std::string_view) -> http_response {
            c_called.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("alias-also-throws");
        };

    webserver ws{create_webserver(PORT)
        .internal_error_handler(alias_c)
        .log_error(logger)};

    auto ha = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const handler_exception_ctx&)>(
            [&a_called](const handler_exception_ctx&) {
                a_called.fetch_add(1, std::memory_order_relaxed);
                return hook_action::pass();
            }));

    auto hb = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const handler_exception_ctx&)>(
            [&b_called](const handler_exception_ctx&) -> hook_action {
                b_called.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("bravo-throws");
            }));

    auto resource = std::make_shared<throwing_resource>();
    ws.register_path("/boom", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/boom";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    ws.stop();

    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(http_code, 500L);
    LT_CHECK_EQ(resp_body, std::string(""));
    LT_CHECK_EQ(a_called.load(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(b_called.load(), static_cast<std::size_t>(1));
    // C must have been called exactly once -- not twice. This pins that
    // the dispatcher does NOT re-enter run_internal_error_handler_safely
    // after the alias-slot has already invoked the user callable.
    LT_CHECK_EQ(c_called.load(), static_cast<std::size_t>(1));

    std::string log_buf = cap.read();
    LT_CHECK_NEQ(log_buf.find("bravo-throws"), std::string::npos);
    LT_CHECK_NEQ(log_buf.find("alias-also-throws"), std::string::npos);
LT_END_AUTO_TEST(all_hooks_fail_yields_hardcoded_empty_500)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
