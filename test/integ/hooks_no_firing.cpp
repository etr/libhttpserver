/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-045 integration sentinel — narrowed by TASK-046.
//
// Originally:
//   "No phase actually fires yet — verified by registering one hook on
//    every phase and observing zero invocations across a complete
//    request/response cycle. (Phases start firing in TASK-046..051.)"
//
// TASK-046 wired the three connection-level phases (connection_opened,
// accept_decision, connection_closed), so the gate now narrows to the
// remaining EIGHT phases that TASK-047..051 will wire. Those eight
// must still observe zero invocations across a complete round-trip.
// We still register all eleven hooks so we keep one-call-site coverage
// of the API surface; we just assert silence on the not-yet-wired
// phases only.

#include <curl/curl.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
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
using httpserver::webserver;

#define PORT 8198

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

LT_BEGIN_SUITE(hooks_no_firing_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(hooks_no_firing_suite)

LT_BEGIN_AUTO_TEST(hooks_no_firing_suite, all_phases_silent_across_round_trip)
    webserver ws{create_webserver(PORT)};

    // Eleven counters indexed by phase ordinal.
    std::array<std::atomic<std::size_t>, 11> counters{};
    for (auto& c : counters) c.store(0);

    auto bump = [&](hook_phase p) {
        counters[static_cast<std::size_t>(p)].fetch_add(
            1, std::memory_order_relaxed);
    };

    auto h1 = ws.add_hook(hook_phase::connection_opened,
        std::function<void(const httpserver::connection_open_ctx&)>(
            [&](const httpserver::connection_open_ctx&) {
                bump(hook_phase::connection_opened);
            }));
    auto h2 = ws.add_hook(hook_phase::accept_decision,
        std::function<void(const httpserver::accept_ctx&)>(
            [&](const httpserver::accept_ctx&) {
                bump(hook_phase::accept_decision);
            }));
    auto h3 = ws.add_hook(hook_phase::request_received,
        std::function<hook_action(httpserver::request_received_ctx&)>(
            [&](httpserver::request_received_ctx&) {
                bump(hook_phase::request_received);
                return hook_action{};
            }));
    auto h4 = ws.add_hook(hook_phase::body_chunk,
        std::function<hook_action(httpserver::body_chunk_ctx&)>(
            [&](httpserver::body_chunk_ctx&) {
                bump(hook_phase::body_chunk);
                return hook_action{};
            }));
    auto h5 = ws.add_hook(hook_phase::route_resolved,
        std::function<void(const httpserver::route_resolved_ctx&)>(
            [&](const httpserver::route_resolved_ctx&) {
                bump(hook_phase::route_resolved);
            }));
    auto h6 = ws.add_hook(hook_phase::before_handler,
        std::function<hook_action(httpserver::before_handler_ctx&)>(
            [&](httpserver::before_handler_ctx&) {
                bump(hook_phase::before_handler);
                return hook_action{};
            }));
    auto h7 = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const httpserver::handler_exception_ctx&)>(
            [&](const httpserver::handler_exception_ctx&) {
                bump(hook_phase::handler_exception);
                return hook_action{};
            }));
    auto h8 = ws.add_hook(hook_phase::after_handler,
        std::function<hook_action(httpserver::after_handler_ctx&)>(
            [&](httpserver::after_handler_ctx&) {
                bump(hook_phase::after_handler);
                return hook_action{};
            }));
    auto h9 = ws.add_hook(hook_phase::response_sent,
        std::function<void(const httpserver::response_sent_ctx&)>(
            [&](const httpserver::response_sent_ctx&) {
                bump(hook_phase::response_sent);
            }));
    auto h10 = ws.add_hook(hook_phase::request_completed,
        std::function<void(const httpserver::request_completed_ctx&)>(
            [&](const httpserver::request_completed_ctx&) {
                bump(hook_phase::request_completed);
            }));
    auto h11 = ws.add_hook(hook_phase::connection_closed,
        std::function<void(const httpserver::connection_close_ctx&)>(
            [&](const httpserver::connection_close_ctx&) {
                bump(hook_phase::connection_closed);
            }));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hello", resource);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/hello";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    std::string body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    LT_CHECK_EQ(res, CURLE_OK);
    LT_CHECK_EQ(body, "OK");

    ws.stop();

    // TASK-046 carved out the three lifecycle phases. TASK-047 added
    // request_received and body_chunk (the GET round-trip below fires
    // request_received for every request; body_chunk does not fire on
    // a GET, but we exclude it for symmetry with the wiring contract).
    // TASK-048 added route_resolved (fires on every request) and
    // before_handler (fires on every request hit). TASK-049 added
    // handler_exception, but it only fires when the handler throws --
    // the successful GET below does not exercise it -- so the silence
    // assertion would still hold for it. We exclude it from
    // not_yet_wired anyway to reflect the implementation contract.
    // TASK-050 wires after_handler, response_sent, and request_completed
    // -- all three fire on the happy-path GET below, so they too are now
    // excluded from not_yet_wired. The predicate now returns false for
    // every phase that TASK-046..051 has put on the wire; only the
    // residual unwired phases (if any) would still pin silence.
    auto not_yet_wired = [](hook_phase p) {
        switch (p) {
        case hook_phase::connection_opened:
        case hook_phase::accept_decision:
        case hook_phase::connection_closed:
        case hook_phase::request_received:
        case hook_phase::body_chunk:
        case hook_phase::route_resolved:
        case hook_phase::before_handler:
        case hook_phase::handler_exception:
        case hook_phase::after_handler:
        case hook_phase::response_sent:
        case hook_phase::request_completed:
            return false;
        default:
            return true;
        }
    };
    for (std::size_t i = 0; i < counters.size(); ++i) {
        if (not_yet_wired(static_cast<hook_phase>(i))) {
            LT_CHECK_EQ(counters[i].load(), static_cast<std::size_t>(0));
        }
    }
LT_END_AUTO_TEST(all_phases_silent_across_round_trip)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
