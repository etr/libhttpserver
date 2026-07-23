/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

#ifndef TEST_INTEG_SERVER_READY_HPP_
#define TEST_INTEG_SERVER_READY_HPP_

// Shared server-ready wait helper for the hooks_* integ tests.
//
// Replaces the legacy `std::this_thread::sleep_for(50ms)` pattern that
// raced with MHD_start_daemon on loaded CI runners and produced
// intermittent CURLE_COULDNT_CONNECT failures.
//
// Design notes:
//
//   * The probe uses CURLOPT_CONNECT_ONLY — i.e. no HTTP request line is
//     ever sent. The kernel completes the TCP three-way handshake and
//     the socket is then closed without an HTTP exchange. This matters
//     because several callers count user-hook firings with exact-count
//     assertions on phases that fire on an HTTP round-trip
//     (request_received, route_resolved, before_handler, after_handler,
//     response_sent, request_completed). A HEAD-based probe would
//     pollute those counters and silently break the tests.
//
//   * The probe DOES fire the connection-level phases on the server
//     under test: connection_opened, accept_decision, connection_closed
//     (MHD accepts the fd, then closes it when the HTTP request never
//     materialises). Callers that count these phases must use `>= N`
//     assertions, not exact-count, OR tolerate one extra probe-induced
//     firing. hooks_connection_lifecycle.cpp and hooks_no_firing.cpp
//     already use bookend / `>= N` semantics and are not affected.
//
//   * The "ready" predicate is `rc != CURLE_COULDNT_CONNECT`. CURLE_OK
//     means TCP came up cleanly; any other libcurl return code except
//     CURLE_COULDNT_CONNECT means the TCP stack is up but something
//     else further up the stack misbehaved (e.g. TLS not negotiated) —
//     still acceptable evidence that the daemon is listening.
//
//   * Works correctly even when the caller has invoked
//     `ws.deny_ip("127.0.0.1")`: the kernel completes the TCP
//     handshake before MHD's policy_callback runs, so the probe still
//     returns OK.
//
//   * The 3000 ms deadline matches the original hooks-test value and
//     gives slow CI lanes (TSan, valgrind) generous headroom. Override
//     by passing an explicit `deadline` argument.
//
// Function is `inline` because the header is included from multiple
// translation units in the test suite.

#include <curl/curl.h>

#include <chrono>
#include <string>
#include <thread>

namespace httpserver_test {

inline void wait_for_server_ready(
        int port,
        std::chrono::milliseconds deadline
            = std::chrono::milliseconds(3000)) {
    using clock = std::chrono::steady_clock;
    const auto end = clock::now() + deadline;
    const std::string url
        = "http://127.0.0.1:" + std::to_string(port) + "/";

    while (clock::now() < end) {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // TCP-only probe: no HTTP request is sent on the wire.
        curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 100L);
        const CURLcode rc = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (rc != CURLE_COULDNT_CONNECT) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

}  // namespace httpserver_test

#endif  // TEST_INTEG_SERVER_READY_HPP_
