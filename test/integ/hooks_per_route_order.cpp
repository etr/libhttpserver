/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// Contract under test:
//
// "New integ test hooks_per_route_order: registers a server-wide
//  response_sent hook A and a per-route response_sent hook B on
//  resource R. A request to R fires A -> B in that order. A request
//  to another resource fires only A."
//
// Closes PRD-HOOK-REQ-006.

#include <curl/curl.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./server_ready.hpp"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::response_sent_ctx;
using httpserver::webserver;

#define PORT 8243

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class r_resource : public http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("r-body");
    }
};

class s_resource : public http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("s-body");
    }
};

std::mutex order_mtx;
std::vector<std::string> firing_order;

void note(const char* label) {
    std::lock_guard<std::mutex> g(order_mtx);
    firing_order.emplace_back(label);
}

}  // namespace

LT_BEGIN_SUITE(hooks_per_route_order_suite)
    void set_up() {
        std::lock_guard<std::mutex> g(order_mtx);
        firing_order.clear();
    }
    void tear_down() {}
LT_END_SUITE(hooks_per_route_order_suite)

LT_BEGIN_AUTO_TEST(hooks_per_route_order_suite, order_per_route_after_global)
    webserver ws{create_webserver(PORT)};

    auto r = std::make_shared<r_resource>();
    auto s = std::make_shared<s_resource>();

    // Server-wide hook A: appends "A".
    auto a_handle = ws.add_hook(hook_phase::response_sent,
        std::function<void(const response_sent_ctx&)>(
            [](const response_sent_ctx&) { note("A"); }));

    // Per-route hook B on resource R: appends "B".
    auto b_handle = r->add_hook(hook_phase::response_sent,
        std::function<void(const response_sent_ctx&)>(
            [](const response_sent_ctx&) { note("B"); }));

    ws.register_path("/r", r);
    ws.register_path("/s", s);
    ws.start(false);
    httpserver_test::wait_for_server_ready(PORT);

    // Request 1: GET /r -> A then B.
    {
        CURL* curl = curl_easy_init();
        LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
        std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/r";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        std::string body;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        LT_CHECK_EQ(res, CURLE_OK);
        LT_CHECK_EQ(body, std::string("r-body"));
    }

    {
        std::lock_guard<std::mutex> g(order_mtx);
        LT_CHECK_EQ(firing_order.size(), static_cast<std::size_t>(2));
        if (firing_order.size() == 2) {
            LT_CHECK_EQ(firing_order[0], std::string("A"));
            LT_CHECK_EQ(firing_order[1], std::string("B"));
        }
        firing_order.clear();
    }

    // Request 2: GET /s -> only A (per-route hook B is on R, not S).
    {
        CURL* curl = curl_easy_init();
        LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
        std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/s";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        std::string body;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        LT_CHECK_EQ(res, CURLE_OK);
        LT_CHECK_EQ(body, std::string("s-body"));
    }

    {
        std::lock_guard<std::mutex> g(order_mtx);
        LT_CHECK_EQ(firing_order.size(), static_cast<std::size_t>(1));
        if (!firing_order.empty()) {
            LT_CHECK_EQ(firing_order[0], std::string("A"));
        }
    }

    ws.stop();
LT_END_AUTO_TEST(order_per_route_after_global)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
