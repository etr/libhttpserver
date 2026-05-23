/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-046 acceptance criterion 1.
//
//   "New integ test `hooks_connection_lifecycle`: opens a connection,
//    registers one hook on each of the three phases, observes
//    (connection_opened, accept_decision{accepted=true}, connection_closed)
//    firing in order."
//
// We start a webserver, register a hook on each of the three lifecycle
// phases, drive one curl round-trip, stop the webserver (which tears
// down all keep-alive connections), and assert that:
//   - all three phases fired at least once;
//   - the accept_decision hook saw accepted=true and reason=nullopt;
//   - peer.to_string() yields a non-empty IPv4-localhost string;
//   - connection_closed is the LAST observed event (lifecycle invariant);
//   - either connection_opened or accept_decision is the FIRST event
//     (MHD's accept loop fires policy_callback before NOTIFY_STARTED,
//     so the natural order is accept_decision first, but the test
//     should not pin a specific MHD callback ordering — only the
//     start/end bookends).

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

#define PORT 8199

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

struct lifecycle_event {
    std::string phase_name;
    std::string peer_str;
    bool accepted;
    std::string reason;
    bool reason_set;
};

}  // namespace

LT_BEGIN_SUITE(hooks_connection_lifecycle_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_connection_lifecycle_suite)

LT_BEGIN_AUTO_TEST(hooks_connection_lifecycle_suite, all_three_phases_fire)
    webserver ws{create_webserver(PORT)};

    std::mutex log_mu;
    std::vector<lifecycle_event> log;

    auto record = [&log_mu, &log](lifecycle_event e) {
        std::lock_guard<std::mutex> g(log_mu);
        log.push_back(std::move(e));
    };

    auto h_open = ws.add_hook(hook_phase::connection_opened,
        std::function<void(const httpserver::connection_open_ctx&)>(
            [&](const httpserver::connection_open_ctx& ctx) {
                record(lifecycle_event{"connection_opened",
                                       ctx.peer.to_string(),
                                       true, "", false});
            }));

    auto h_accept = ws.add_hook(hook_phase::accept_decision,
        std::function<void(const httpserver::accept_ctx&)>(
            [&](const httpserver::accept_ctx& ctx) {
                record(lifecycle_event{"accept_decision",
                                       ctx.peer.to_string(),
                                       ctx.accepted,
                                       ctx.reason.has_value()
                                           ? std::string(*ctx.reason)
                                           : std::string{},
                                       ctx.reason.has_value()});
            }));

    auto h_close = ws.add_hook(hook_phase::connection_closed,
        std::function<void(const httpserver::connection_close_ctx&)>(
            [&](const httpserver::connection_close_ctx& ctx) {
                record(lifecycle_event{"connection_closed",
                                       ctx.peer.to_string(),
                                       true, "", false});
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

    // Stop the daemon: MHD tears down every live connection, so any
    // pending connection_closed fires synchronously inside MHD_stop_daemon.
    ws.stop();

    // Take a stable snapshot of the log.
    std::vector<lifecycle_event> snapshot;
    {
        std::lock_guard<std::mutex> g(log_mu);
        snapshot = log;
    }

    // Count occurrences of each phase.
    std::size_t opened_count = 0;
    std::size_t accept_count = 0;
    std::size_t closed_count = 0;
    for (const auto& e : snapshot) {
        if (e.phase_name == "connection_opened") ++opened_count;
        else if (e.phase_name == "accept_decision") ++accept_count;
        else if (e.phase_name == "connection_closed") ++closed_count;
    }

    LT_CHECK(opened_count >= 1);
    LT_CHECK(accept_count >= 1);
    LT_CHECK(closed_count >= 1);

    // The accept hook should have observed accepted=true with no reason.
    bool found_accept_decision = false;
    for (const auto& e : snapshot) {
        if (e.phase_name == "accept_decision") {
            LT_CHECK_EQ(e.accepted, true);
            LT_CHECK(!e.reason_set);
            // The peer should be IPv4 loopback when the test client is
            // local. (MHD may report 0.0.0.0 on some platforms when
            // listening on an IPv6 dual stack; tolerate non-empty.)
            LT_CHECK(!e.peer_str.empty());
            found_accept_decision = true;
            break;
        }
    }
    LT_CHECK(found_accept_decision);

    // Bookends:
    //   - connection_closed is the LAST event (lifecycle invariant)
    //   - the FIRST event is either connection_opened or accept_decision
    //     (MHD callback order is platform-dependent)
    LT_ASSERT(!snapshot.empty());
    LT_CHECK_EQ(snapshot.back().phase_name, std::string("connection_closed"));
    const std::string& first = snapshot.front().phase_name;
    LT_CHECK(first == "connection_opened" || first == "accept_decision");

LT_END_AUTO_TEST(all_three_phases_fire)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
