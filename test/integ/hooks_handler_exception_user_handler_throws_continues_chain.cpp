/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-049 acceptance criterion 2.
//
// "New integ test hooks_handler_exception_user_handler_throws_continues_chain:
//  A throws; B is still invoked; B's response is queued."
//
// Per DR-012 §4.10, handler_exception is the one phase where an
// exception-in-handler-of-exception does NOT abort the chain. A throwing
// hook is caught, logged via log_error, and the chain continues to the
// next hook. We pin both behaviours: B's response is observed on the
// wire AND the log_error capture contains A's throw message.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
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

#define PORT 8231

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

LT_BEGIN_SUITE(hooks_handler_exception_user_throws_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_handler_exception_user_throws_suite)

LT_BEGIN_AUTO_TEST(hooks_handler_exception_user_throws_suite,
                   throwing_hook_caught_chain_continues_to_next)
    std::atomic<std::size_t> b_called{0};
    log_capture cap;
    auto logger = [&cap](const std::string& msg) { cap.append(msg); };

    // No internal_error_handler on the builder — the alias slot stays empty.
    webserver ws{create_webserver(PORT).log_error(logger)};

    auto ha = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const handler_exception_ctx&)>(
            [](const handler_exception_ctx&) -> hook_action {
                throw std::runtime_error("alpha");
            }));

    auto hb = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const handler_exception_ctx&)>(
            [&b_called](const handler_exception_ctx&) {
                b_called.fetch_add(1, std::memory_order_relaxed);
                return hook_action::respond_with(
                    http_response::string("BRAVO").with_status(418));
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
    LT_CHECK_EQ(http_code, 418L);
    LT_CHECK_EQ(resp_body, std::string("BRAVO"));
    LT_CHECK_EQ(b_called.load(), static_cast<std::size_t>(1));
    // The log_error capture must mention A's "alpha" throw.
    std::string log_buf = cap.read();
    LT_CHECK_NEQ(log_buf.find("alpha"), std::string::npos);
LT_END_AUTO_TEST(throwing_hook_caught_chain_continues_to_next)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
