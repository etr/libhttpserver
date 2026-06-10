/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-068 integ test: a basic-auth username carried on a first request
// (the canonical CWE-226 channel -- decoded credentials are stored in
// pmr::string members of http_request_impl that live in the per-
// connection arena) is NOT observable from a subsequent request on the
// same keep-alive MHD connection.
//
// The arena (connection_state::initial_buffer_) backs the
// http_request_impl::username / password / digested_user fields. When
// get_user() / get_pass() is called on the first request, MHD decodes
// the Authorization: Basic <b64> value and the result lands in the
// pmr::string -- which is allocated in the arena. After
// reset_arena() runs (in request_completed), those bytes must be
// scrubbed before the next request reuses the same memory.
//
// Strategy:
//   - GET 1 with Authorization: Basic <b64(SENTINEL:x)>. The handler
//     calls get_user() (which populates the arena-backed username
//     pmr::string), then peeks initial_buffer_. Sanity assertion: the
//     sentinel byte sequence must be present.
//   - GET 2 with no Authorization header. The handler peeks
//     initial_buffer_. Headline assertion: the sentinel must NOT be
//     observable.
//   - A connection_opened server-wide hook fires exactly once across
//     both requests (belt-and-braces signal that keep-alive engaged).

#if defined(_WIN32) && !defined(__CYGWIN__)
#define _WINDOWS
#endif

#include <curl/curl.h>
// HTTPSERVER_COMPILATION supplied by test/Makefile.am AM_CPPFLAGS, which
// lets the handler reach into the internal connection_state and the new
// underlying_connection_for_testing() accessor.
#include <microhttpd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "httpserver/detail/connection_state.hpp"

#ifdef HTTPSERVER_PORT
#define PORT HTTPSERVER_PORT
#else
#define PORT 8080
#endif

namespace {

std::atomic<bool> g_first_handler_saw_sentinel{false};
std::atomic<bool> g_second_handler_saw_sentinel{false};
std::atomic<int> g_request_count{0};
std::atomic<int> g_connection_opened_count{0};

// Printable sentinel used as the basic-auth username on the first
// request. Long enough to be unambiguous when grep-ing for it.
constexpr const char kSentinel[] = "DEADBEEFCRED_SENTINEL_USERNAME_FROM_REQUEST_1";  // NOLINT(whitespace/line_length)

bool buffer_contains_sentinel(const std::byte* p, std::size_t n) noexcept {
    constexpr std::size_t k = sizeof(kSentinel) - 1;
    if (n < k) return false;
    const auto* b = reinterpret_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i + k <= n; ++i) {
        if (std::memcmp(b + i, kSentinel, k) == 0) {
            return true;
        }
    }
    return false;
}

class peek_resource : public httpserver::http_resource {
 public:
    httpserver::http_response render_get(
        const httpserver::http_request& req) override {
        const int n = ++g_request_count;
        MHD_Connection* mhd_conn = req.underlying_connection_for_testing();
        if (mhd_conn == nullptr) {
            return httpserver::http_response::string("no-conn");
        }
        const MHD_ConnectionInfo* ci = MHD_get_connection_info(
            mhd_conn, MHD_CONNECTION_INFO_SOCKET_CONTEXT);
        if (ci == nullptr || ci->socket_context == nullptr) {
            return httpserver::http_response::string("no-ctx");
        }
        auto* cs = static_cast<httpserver::detail::connection_state*>(
            ci->socket_context);
        // Touch get_user() once so the lazy basic-auth credential cache
        // populates the arena-backed username pmr::string. On request 1
        // (Authorization header present) this is where the sentinel
        // lands; on request 2 (no Authorization header) this is a no-op
        // returning an empty string.
        (void)req.get_user();
        const bool found =
            buffer_contains_sentinel(cs->initial_buffer_.data(),
                                     cs->initial_buffer_.size());
        if (n == 1) {
            g_first_handler_saw_sentinel.store(found);
        } else if (n == 2) {
            g_second_handler_saw_sentinel.store(found);
        }
        return httpserver::http_response::string("OK");
    }
};

}  // namespace

LT_BEGIN_SUITE(connection_state_body_residue_suite)
    void set_up() {
        g_first_handler_saw_sentinel.store(false);
        g_second_handler_saw_sentinel.store(false);
        g_request_count.store(0);
        g_connection_opened_count.store(0);
    }
    void tear_down() {}
LT_END_SUITE(connection_state_body_residue_suite)

LT_BEGIN_AUTO_TEST(connection_state_body_residue_suite,
                   sentinel_in_first_body_not_observable_in_second)
// Connection-lifecycle hook firing on MSYS curl + MHD has not been verified,
// and the test depends on the connection_opened hook running through MHD's
// Windows accept path. Coverage on Linux / Darwin remains unaffected.
// reason: see test/PORTABILITY.md §connection_state_body_residue_test.cpp.
#ifndef _WINDOWS
    httpserver::webserver ws{httpserver::create_webserver(PORT)};

    auto on_open = ws.add_hook(
        httpserver::hook_phase::connection_opened,
        std::function<void(const httpserver::connection_open_ctx&)>(
            [](const httpserver::connection_open_ctx&) {
                g_connection_opened_count.fetch_add(1);
            }));
    (void)on_open;

    auto r = std::make_shared<peek_resource>();
    ws.register_path("/peek", r);
    ws.start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CURL* curl = curl_easy_init();
    LT_ASSERT_NEQ(curl, static_cast<CURL*>(nullptr));
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/peek";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    // Force HTTP/1.1 to guarantee keep-alive semantics (HTTP/2 has
    // stream multiplexing rather than connection reuse in the HTTP/1.1
    // sense, so the connection_opened hook count would differ).
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    // GET 1: carry the sentinel string as the basic-auth username. The
    // handler calls get_user() which copies it into the arena-backed
    // pmr::string username field.
    std::string userpwd = std::string(kSentinel) + ":x";
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
    CURLcode res1 = curl_easy_perform(curl);
    LT_CHECK_EQ(res1, CURLE_OK);

    // GET 2: no Authorization header. The handler peeks
    // initial_buffer_; the headline assertion is that the prior
    // request's username has been scrubbed by reset_arena() and is
    // not observable.
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_NONE);
    curl_easy_setopt(curl, CURLOPT_USERPWD, "");
    CURLcode res2 = curl_easy_perform(curl);
    LT_CHECK_EQ(res2, CURLE_OK);

    curl_easy_cleanup(curl);
    ws.stop();

    // Gate: keep-alive must have engaged. Both requests must share the
    // same connection_state arena for the headline assertion to be
    // meaningful. Use ASSERT (hard abort) so that if keep-alive did not
    // engage, we abort here rather than evaluating the headline assertion
    // under the wrong precondition and producing a vacuously-passing
    // misleading result (a new connection gives a zeroed arena regardless
    // of whether reset_arena scrubs credentials).
    LT_ASSERT_EQ(g_connection_opened_count.load(), 1);
    // Sanity: first handler observed the sentinel in its own request's
    // body (proves the buffer is the one we are testing and get_user()
    // populated the arena-backed username field as expected).
    LT_CHECK(g_first_handler_saw_sentinel.load());
    // Headline assertion: by the time the second request reaches the
    // handler on the same connection, the buffer has been cleared by
    // reset_arena() and the credential sentinel is not observable.
    LT_CHECK(!g_second_handler_saw_sentinel.load());
#endif
LT_END_AUTO_TEST(sentinel_in_first_body_not_observable_in_second)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
