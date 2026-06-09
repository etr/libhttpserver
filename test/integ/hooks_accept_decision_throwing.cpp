/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-046 acceptance criterion 3.
//
//   "A throwing accept_decision hook does not flip the accept/reject
//    decision (the connection is still rejected per policy_callback's
//    return value)."
//
// Two sub-scenarios:
//   (a) Banned 127.0.0.1 + throwing hook: curl request still rejected.
//   (b) Unbanned 127.0.0.1 + throwing hook: curl request still accepted
//       and returns "OK".
//
// The structural guarantee is enforced by sequencing: the MHD_Result
// `decision` variable in policy_callback is fixed BEFORE fire_accept_decision
// runs, and fire_accept_decision is declared noexcept with a catch-all.
// This test is a regression gate against a future refactor that
// accidentally reordered the fire/return.

#include <curl/curl.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./server_ready.hpp"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;
using httpserver::http::http_utils;

// Each sub-test gets its own named port constant so adding a third test
// in the future does not require renaming the existing arithmetic.
#define PORT_BANNED   8201
#define PORT_ACCEPTED 8202

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class hello_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("OK");
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_accept_decision_throwing_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_accept_decision_throwing_suite)

LT_BEGIN_AUTO_TEST(hooks_accept_decision_throwing_suite,
                   banned_with_throwing_hook_still_rejected)
    webserver ws{create_webserver(PORT_BANNED)
                     .default_policy(http_utils::ACCEPT)};
    ws.block_ip("127.0.0.1");

    std::atomic<std::size_t> fired{0};
    auto h = ws.add_hook(hook_phase::accept_decision,
        std::function<void(const httpserver::accept_ctx&)>(
            [&](const httpserver::accept_ctx&) {
                fired.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error(
                    "hook intentionally throwing in test");
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hello", resource);
    ws.start(false);
    // TCP-level probe — works even though ws.block_ip("127.0.0.1") rejects HTTP
    // probes (the kernel completes the handshake before MHD's policy_callback runs).
    httpserver_test::wait_for_server_ready(PORT_BANNED);

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT_BANNED) + "/hello";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    // Decision must remain MHD_NO (rejected) even though the hook threw.
    LT_CHECK_NEQ(res, CURLE_OK);
    LT_CHECK(fired.load() >= 1);

    ws.stop();
LT_END_AUTO_TEST(banned_with_throwing_hook_still_rejected)

LT_BEGIN_AUTO_TEST(hooks_accept_decision_throwing_suite,
                   unbanned_with_throwing_hook_still_accepted)
    webserver ws{create_webserver(PORT_ACCEPTED)
                     .default_policy(http_utils::ACCEPT)};

    std::atomic<std::size_t> fired{0};
    auto h = ws.add_hook(hook_phase::accept_decision,
        std::function<void(const httpserver::accept_ctx&)>(
            [&](const httpserver::accept_ctx&) {
                fired.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error(
                    "hook intentionally throwing in test");
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hello", resource);
    ws.start(false);
    httpserver_test::wait_for_server_ready(PORT_ACCEPTED);

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT_ACCEPTED) + "/hello";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    // Decision must remain MHD_YES (accepted) even though the hook threw.
    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(body, std::string("OK"));
    LT_CHECK(fired.load() >= 1);

    ws.stop();
LT_END_AUTO_TEST(unbanned_with_throwing_hook_still_accepted)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
