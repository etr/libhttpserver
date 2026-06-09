/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-049 acceptance criterion 1.
//
// "New integ test hooks_handler_exception_chain: registers two
//  handler_exception hooks (A returns pass(), B returns
//  respond_with(418_response)) and an internal_error_handler alias (C). A
//  handler that throws std::runtime_error("boom") triggers A -> B; B wins;
//  C is never called."
//
// The chain order is: user hooks in registration order, then the
// internal_error_handler alias slot LAST. A is registered first, B
// second; B short-circuits with a 418, so C must never be invoked.

#include <curl/curl.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./server_ready.hpp"

using httpserver::create_webserver;
using httpserver::handler_exception_ctx;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8230

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

}  // namespace

LT_BEGIN_SUITE(hooks_handler_exception_chain_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_handler_exception_chain_suite)

LT_BEGIN_AUTO_TEST(hooks_handler_exception_chain_suite,
                   first_responding_hook_wins_alias_never_invoked)
    std::atomic<std::size_t> a_called{0};
    std::atomic<std::size_t> b_called{0};
    std::atomic<std::size_t> c_called{0};

    auto alias_c = [&c_called](const http_request&,
                               std::string_view) -> http_response {
        c_called.fetch_add(1, std::memory_order_relaxed);
        return http_response::string("ALIAS-C").with_status(500);
    };

    webserver ws{create_webserver(PORT)
        .internal_error_handler(alias_c)};

    auto ha = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const handler_exception_ctx&)>(
            [&a_called](const handler_exception_ctx&) {
                a_called.fetch_add(1, std::memory_order_relaxed);
                return hook_action::pass();
            }));

    auto hb = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const handler_exception_ctx&)>(
            [&b_called](const handler_exception_ctx&) {
                b_called.fetch_add(1, std::memory_order_relaxed);
                return hook_action::respond_with(
                    http_response::string("TEAPOT").with_status(418));
            }));

    auto resource = std::make_shared<throwing_resource>();
    ws.register_path("/boom", resource);
    ws.start(false);
    httpserver_test::wait_for_server_ready(PORT);

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/boom";
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
    LT_CHECK_EQ(http_code, 418L);
    LT_CHECK_EQ(resp_body, std::string("TEAPOT"));
    LT_CHECK_EQ(a_called.load(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(b_called.load(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(c_called.load(), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(first_responding_hook_wins_alias_never_invoked)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
