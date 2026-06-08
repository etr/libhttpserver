/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-071: end-to-end pin for the not_found_handler -> route_resolved
// alias seat.
//
// Sub-item A from TASK-071 — replace the empty stub callback in
// webserver::install_not_found_alias_ with the wired observation hook
// documented per DR-012 §4.10. The on-wire 404 body still flows through
// webserver_impl::not_found_page (route_resolved is observation-only;
// the response slot is not exposed at this phase), but the alias seat
// in the bus must be a real hook so:
//
//   (1) PRD-HOOK-REQ-009: v1 setters expose themselves as hook-bus
//       subscriptions (verified by hooks_alias_count_test already; we
//       cross-pin the on-wire behaviour here).
//   (2) AC-1: a fresh webserver with no explicit not_found_handler
//       returns 404 with the canonical "Not Found" body.
//   (3) AC-1 (custom branch): a webserver with a user not_found_handler
//       returns 404 with the user-supplied body on a miss path.
//   (4) DR-012 §4.10 ordering pin: user-registered route_resolved hooks
//       still fire alongside the alias seat (observation phase is not
//       short-circuit-capable). A user observation hook must still see
//       the miss.

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::route_resolved_ctx;
using httpserver::webserver;

#define PORT_DEFAULT 8211
#define PORT_CUSTOM  8212
#define PORT_ORDER   8213

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

CURLcode get_url(int port, const std::string& path,
                 long* http_code,  // NOLINT(runtime/int)
                 std::string* body) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return CURLE_FAILED_INIT;
    std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    CURLcode rc = curl_easy_perform(curl);
    if (http_code != nullptr) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_code);
    }
    curl_easy_cleanup(curl);
    return rc;
}

}  // namespace

LT_BEGIN_SUITE(hooks_not_found_alias_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_not_found_alias_suite)

// AC-1, default branch. With no explicit not_found_handler, a request
// for a missing route returns 404 with the canonical "Not Found" body.
// Pinned end-to-end so any future structural refactor of
// install_not_found_alias_ cannot silently regress the wire contract.
LT_BEGIN_AUTO_TEST(hooks_not_found_alias_suite,
                   not_found_alias_default_body_when_no_user_handler)
    webserver ws{create_webserver(PORT_DEFAULT)};
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    long code = 0;  // NOLINT(runtime/int)
    std::string body;
    LT_CHECK_EQ(get_url(PORT_DEFAULT, "/nonexistent", &code, &body), CURLE_OK);

    ws.stop();

    LT_CHECK_EQ(code, 404L);
    // constants::NOT_FOUND_ERROR == "Not Found" (src/httpserver/constants.hpp:51).
    LT_CHECK_EQ(body, std::string("Not Found"));
LT_END_AUTO_TEST(not_found_alias_default_body_when_no_user_handler)

// AC-1, custom branch. With an explicit not_found_handler, a request for
// a missing route returns 404 with the user-supplied body. The alias
// seat is wired (verified separately by hooks_alias_count_test), and the
// on-wire 404 body flows through webserver_impl::not_found_page calling
// the same handler — the user handler is invoked exactly once per miss
// (not double-counted by the alias hook body, which is observation-only).
LT_BEGIN_AUTO_TEST(hooks_not_found_alias_suite,
                   not_found_alias_invokes_user_handler_on_miss_with_v1_body)
    std::atomic<int> handler_calls{0};
    auto user_not_found = [&handler_calls](const http_request&) {
        handler_calls.fetch_add(1, std::memory_order_relaxed);
        return http_response::string("CUSTOM_404").with_status(404);
    };

    webserver ws{create_webserver(PORT_CUSTOM).not_found_handler(user_not_found)};
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    long code = 0;  // NOLINT(runtime/int)
    std::string body;
    LT_CHECK_EQ(get_url(PORT_CUSTOM, "/nonexistent", &code, &body), CURLE_OK);

    ws.stop();

    LT_CHECK_EQ(code, 404L);
    LT_CHECK_EQ(body, std::string("CUSTOM_404"));
    // The user handler fires exactly once. The alias hook body is a
    // pure observation marker (DR-012 §4.10); it does NOT re-invoke
    // the user handler. The on-wire body comes from not_found_page.
    LT_CHECK_EQ(handler_calls.load(), 1);
LT_END_AUTO_TEST(not_found_alias_invokes_user_handler_on_miss_with_v1_body)

// DR-012 §4.10 ordering pin. The alias seat installs hook[0] at
// route_resolved when not_found_handler is set; a user-added observation
// hook becomes hook[1]. Both fire on a miss (observation phase is not
// short-circuit-capable). Cross-pins that the alias upgrade did not
// accidentally suppress co-registered user hooks.
LT_BEGIN_AUTO_TEST(hooks_not_found_alias_suite,
                   not_found_alias_does_not_suppress_user_route_resolved_hook)
    std::atomic<int> user_observer_calls{0};
    std::atomic<int> user_observer_misses{0};
    auto user_not_found = [](const http_request&) {
        return http_response::string("CUSTOM_404").with_status(404);
    };

    webserver ws{create_webserver(PORT_ORDER).not_found_handler(user_not_found)};

    auto h = ws.add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [&user_observer_calls, &user_observer_misses]
            (const route_resolved_ctx& ctx) {
                user_observer_calls.fetch_add(1, std::memory_order_relaxed);
                if (!ctx.matched.has_value()) {
                    user_observer_misses.fetch_add(1,
                                                   std::memory_order_relaxed);
                }
            }));

    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    long code = 0;  // NOLINT(runtime/int)
    std::string body;
    LT_CHECK_EQ(get_url(PORT_ORDER, "/nonexistent", &code, &body), CURLE_OK);

    ws.stop();

    LT_CHECK_EQ(code, 404L);
    LT_CHECK_EQ(body, std::string("CUSTOM_404"));
    // User observation hook fired once on the miss path.
    LT_CHECK_EQ(user_observer_calls.load(), 1);
    LT_CHECK_EQ(user_observer_misses.load(), 1);
LT_END_AUTO_TEST(not_found_alias_does_not_suppress_user_route_resolved_hook)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
