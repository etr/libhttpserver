/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-046 acceptance criterion 2.
//
//   "New integ test `hooks_accept_decision_banned`: with the IP ban
//    list populated and the default policy ACCEPT, an `accept_decision`
//    hook observes `accepted=false, reason="banned"`."
//
// Closes issue #332 — emit a log entry per banned-IP rejection.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;
using httpserver::http::http_utils;

#define PORT 8200

namespace {

struct observation {
    bool accepted;
    bool reason_set;
    std::string reason;
    std::string peer;
};

class hello_resource : public httpserver::http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("OK");
    }
};

}  // namespace

LT_BEGIN_SUITE(hooks_accept_decision_banned_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_accept_decision_banned_suite)

LT_BEGIN_AUTO_TEST(hooks_accept_decision_banned_suite, banned_ip_observed)
    webserver ws{create_webserver(PORT)
                     .default_policy(http_utils::ACCEPT)};
    ws.block_ip("127.0.0.1");

    std::mutex mu;
    std::vector<observation> log;

    auto h_accept = ws.add_hook(hook_phase::accept_decision,
        std::function<void(const httpserver::accept_ctx&)>(
            [&](const httpserver::accept_ctx& ctx) {
                std::lock_guard<std::mutex> g(mu);
                log.push_back(observation{
                    ctx.accepted,
                    ctx.reason.has_value(),
                    ctx.reason.has_value() ? std::string(*ctx.reason)
                                           : std::string{},
                    ctx.peer.to_string(),
                });
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hello", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Issue a curl request from 127.0.0.1. The connection MUST be
    // rejected by policy_callback before answer_to_connection ever
    // fires; the accept_decision hook observes the rejection.
    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/hello";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // Short timeout so the assertion failure (if any) shows up fast.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    // We expect a curl error (the daemon either drops the connection
    // or returns a non-200; in either case the policy rejected it).
    LT_CHECK_NEQ(res, CURLE_OK);

    ws.stop();

    std::vector<observation> snapshot;
    {
        std::lock_guard<std::mutex> g(mu);
        snapshot = log;
    }

    // The hook should have fired at least once with accepted=false /
    // reason="banned" for the rejected attempt.
    bool found_banned = false;
    for (const auto& o : snapshot) {
        if (!o.accepted && o.reason_set && o.reason == "banned") {
            found_banned = true;
            // Sanity: peer is the loopback address.
            LT_CHECK_EQ(o.peer, std::string("127.0.0.1"));
        }
    }
    LT_CHECK(found_banned);
LT_END_AUTO_TEST(banned_ip_observed)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
