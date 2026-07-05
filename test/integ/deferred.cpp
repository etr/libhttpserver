/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#if defined(_WIN32) && !defined(__CYGWIN__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <curl/curl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./test_utils.hpp"

using std::shared_ptr;
using std::string;

using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_response;
using httpserver::http_request;
using httpserver::http_resource;

size_t writefunc(void *ptr, size_t size, size_t nmemb, string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

static int counter = 0;

struct test_data {
    int value;
};

ssize_t test_callback(shared_ptr<void> closure_data, char* buf, size_t max) {
    std::ignore = closure_data;

    if (counter == 2) {
        return -1;
    } else {
        memset(buf, 0, max);
        snprintf(buf, max, "%s", "test");
        counter++;
        return string(buf).size();
    }
}

ssize_t test_callback_with_data(shared_ptr<test_data> closure_data, char* buf, size_t max) {
    if (counter == 2) {
        return -1;
    } else {
        memset(buf, 0, max);
        snprintf(buf, max, "%s%s", "test", std::to_string(closure_data->value).c_str());

        closure_data->value = 84;

        counter++;
        return std::string(buf).size();
    }
}

// TASK-013: v1's deferred_response<T> had a typed callable + initial content
// prefix. v2's http_response::deferred(producer) is type-erased through
// std::function and has no initial-content parameter; the lambda below
// reproduces the v1 prefix-then-callback semantics by emitting the prefix
// once before delegating to the typed callback.
class deferred_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         std::string initial = "cycle callback response";
         return http_response::deferred(
             [initial, served = false](std::uint64_t,
                                       char* buf,
                                       std::size_t max) mutable -> ssize_t {
                 if (!served) {
                     served = true;
                     std::size_t n = std::min(initial.size(), max);
                     memcpy(buf, initial.data(), n);
                     return n;
                 }
                 return test_callback(nullptr, buf, max);
             });
     }
};

class deferred_resource_with_data : public http_resource {
 public:
     http_response render_get(const http_request&) {
         auto internal_info = std::make_shared<test_data>();
         internal_info->value = 42;
         std::string initial = "cycle callback response";
         return http_response::deferred(
             [internal_info, initial,
              served = false](std::uint64_t,
                              char* buf,
                              std::size_t max) mutable -> ssize_t {
                 if (!served) {
                     served = true;
                     std::size_t n = std::min(initial.size(), max);
                     memcpy(buf, initial.data(), n);
                     return n;
                 }
                 return test_callback_with_data(internal_info, buf, max);
             });
     }
};

class deferred_resource_empty_content : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::deferred(
             [](std::uint64_t, char* buf, std::size_t max) -> ssize_t {
                 return test_callback(nullptr, buf, max);
             });
     }
};

LT_BEGIN_SUITE(deferred_suite)
    std::unique_ptr<webserver> ws;
    uint16_t port = 0;

    void set_up() {
        ws = std::make_unique<webserver>(create_webserver(0));
        ws->start(false);
        port = ws->get_bound_port();
    }

    void tear_down() {
        counter = 0;

        ws->stop();
    }
LT_END_SUITE(deferred_suite)

LT_BEGIN_AUTO_TEST(deferred_suite, deferred_response_with_prefix_content)
    counter = 0;  // reset per-test; tear_down also resets but order may vary
    auto resource = std::make_shared<deferred_resource>();
    ws->register_path("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    const std::string url = "localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "cycle callback responsetesttest");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(deferred_response_with_prefix_content)

LT_BEGIN_AUTO_TEST(deferred_suite, deferred_response_with_data)
    counter = 0;  // reset per-test; tear_down also resets but order may vary
    auto resource = std::make_shared<deferred_resource_with_data>();
    ws->register_path("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    const std::string url = "localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "cycle callback responsetest42test84");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(deferred_response_with_data)

LT_BEGIN_AUTO_TEST(deferred_suite, deferred_response_empty_content)
    counter = 0;  // reset per-test; tear_down also resets but order may vary
    auto resource = std::make_shared<deferred_resource_empty_content>();
    ws->register_path("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    const std::string url = "localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "testtest");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(deferred_response_empty_content)

// ---------------------------------------------------------------------------
// TASK-036 acceptance tests: handler return-by-value dispatch cutover.
//
// AC-1: lambda registered via on_get returning http_response by value
//       produces 200 with body "hi".
// AC-2: class subclassing http_resource with `http_response render_get(...)
//       override` produces 200.
// AC-3: deferred response's producer callable lives until request_completed
//       fires — verified by a destruction-tracking sentinel in the
//       producer's captures and a sync wait on ws->stop() (which drains MHD
//       and runs request_completed for every pending connection).
// ---------------------------------------------------------------------------

namespace {
// Sentinel whose destructor fires when the http_response (and its
// deferred_body) is destroyed inside ~modded_request(), which fires from
// webserver_impl::request_completed.  On destruction it sets *destroyed to
// true and notifies *cv so the test thread's timed_wait can return
// immediately rather than spinning to a 1-second timeout.
//
// move_constructor leaves all pointers null so the moved-from temporary's
// destructor is a no-op.  Otherwise std::make_shared's in-place copy/move
// of the temporary aggregate would flip `destroyed` BEFORE the lambda runs.
struct destruction_sentinel {
    std::atomic<bool>*     destroyed;
    std::mutex*            cv_mu;
    std::condition_variable* cv;

    explicit destruction_sentinel(std::atomic<bool>* d,
                                  std::mutex* mu,
                                  std::condition_variable* c)
        : destroyed(d), cv_mu(mu), cv(c) {}
    destruction_sentinel(const destruction_sentinel&) = delete;
    destruction_sentinel& operator=(const destruction_sentinel&) = delete;
    destruction_sentinel(destruction_sentinel&& o) noexcept
        : destroyed(std::exchange(o.destroyed, nullptr)),
          cv_mu(std::exchange(o.cv_mu, nullptr)),
          cv(std::exchange(o.cv, nullptr)) {}
    destruction_sentinel& operator=(destruction_sentinel&& o) noexcept {
        destroyed = std::exchange(o.destroyed, nullptr);
        cv_mu     = std::exchange(o.cv_mu,     nullptr);
        cv        = std::exchange(o.cv,         nullptr);
        return *this;
    }
    ~destruction_sentinel() {
        if (!destroyed) return;
        destroyed->store(true);
        // Notify under the mutex so the waiting thread cannot miss the signal
        // if it is between loading destroyed and entering wait.
        {
            std::lock_guard<std::mutex> lk(*cv_mu);
        }
        cv->notify_one();
    }
};

class by_value_class_resource : public http_resource {
 public:
    http_response render_get(const http_request&) override {
        return http_response::string("class-hi");
    }
};
}  // namespace

LT_BEGIN_AUTO_TEST(deferred_suite, on_get_lambda_returns_value)
    // AC-1: lambda returns http_response by value (DR-004) and the dispatch
    // path moves the prvalue into mr->response.
    ws->on_get("/by_value", [](const http_request&) {
        return http_response::string("hi");
    });
    curl_global_init(CURL_GLOBAL_ALL);

    std::string body;
    CURL* curl = curl_easy_init();
    const std::string url = "localhost:" + std::to_string(port) + "/by_value";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(body, "hi");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(on_get_lambda_returns_value)

LT_BEGIN_AUTO_TEST(deferred_suite, on_post_lambda_returns_value)
    // Guard against method-specific dispatch bugs in lambda_resource::invoke_
    // for non-GET verbs. on_post uses the same invoke_() template path as
    // on_get but with http_method::post — one test covers the general case
    // for all remaining verbs (render_post, render_put, render_delete, etc.).
    ws->on_post("/post_by_value", [](const http_request&) {
        return http_response::string("post-ok");
    });
    curl_global_init(CURL_GLOBAL_ALL);

    std::string body;
    CURL* curl = curl_easy_init();
    const std::string url =
        "localhost:" + std::to_string(port) + "/post_by_value";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(body, "post-ok");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(on_post_lambda_returns_value)

LT_BEGIN_AUTO_TEST(deferred_suite, class_render_get_returns_value)
    // AC-2: http_resource subclass with `http_response render_get(...)
    // override` works end-to-end.
    auto resource = std::make_shared<by_value_class_resource>();
    ws->register_path("class_path", resource);
    curl_global_init(CURL_GLOBAL_ALL);

    std::string body;
    CURL* curl = curl_easy_init();
    const std::string url = "localhost:" + std::to_string(port) + "/class_path";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(body, "class-hi");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(class_render_get_returns_value)

LT_BEGIN_AUTO_TEST(deferred_suite, deferred_producer_destroyed_in_request_completed)
    // AC-3 / DR-010: the http_response (and its deferred_body's captured
    // producer state) must outlive every MHD trampoline invocation and be
    // destroyed only when request_completed fires. We pin the contract by
    // capturing a shared_ptr<destruction_sentinel> inside the producer's
    // captures, then asserting the sentinel was destroyed after the server
    // is fully drained.
    //
    // Two-concern note (TASK-036 review #10): this test asserts both
    //   (a) body delivery ("okok" / 200) — the liveness gate
    //   (b) the lifetime contract — the load-bearing pin
    // A failed (a) short-circuits (b). That is intentional: (a) acts as a
    // gating precondition — if the dispatch path doesn't deliver bytes
    // we already know (b) is meaningless. The reviewer recommendation to
    // split into two tests was weighed against duplicating the sentinel
    // wiring (~40 LOC of producer + condvar plumbing) for the marginal
    // benefit of pinpointing which of the two assertions broke. Test
    // names already encode the primary contract; failure output makes
    // the gating relationship obvious.
    //
    // Synchronization: sentinel destructor signals destroyed_cv so the test
    // thread wakes immediately rather than spinning. Upper bound is 5 s to
    // catch a genuine DR-010 regression without hanging the CI suite.
    std::atomic<bool> destroyed{false};
    std::atomic<int>  producer_calls{0};
    std::mutex        destroyed_mu;
    std::condition_variable destroyed_cv;

    // Key contract (DR-010): the producer captures must live until
    // request_completed. Capture the sentinel in the INNER (producer) lambda
    // only — not the outer on_get lambda — so it is destroyed with
    // deferred_body when ~modded_request fires from request_completed.
    ws->on_get("/lifetime",
               [&producer_calls, &destroyed, &destroyed_mu, &destroyed_cv](
                   const http_request&) {
        return http_response::deferred(
            [sentinel = std::make_shared<destruction_sentinel>(
                 &destroyed, &destroyed_mu, &destroyed_cv),
             &producer_calls, &destroyed, served = 0](
                std::uint64_t, char* buf, std::size_t max) mutable -> ssize_t {
                // Defensive: producer must not run after sentinel is gone.
                if (destroyed.load()) {
                    return -1;
                }
                producer_calls.fetch_add(1);
                if (served >= 2) {
                    return -1;  // MHD_CONTENT_READER_END_OF_STREAM
                }
                const char* payload = "ok";
                std::size_t n = std::min<std::size_t>(2, max);
                std::memcpy(buf, payload, n);
                ++served;
                return static_cast<ssize_t>(n);
            });
    });

    curl_global_init(CURL_GLOBAL_ALL);
    std::string body;
    CURL* curl = curl_easy_init();
    const std::string url = "localhost:" + std::to_string(port) + "/lifetime";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    // Force HTTP/1.0 (no keep-alive) so MHD closes the connection as
    // soon as curl finishes reading the body, making request_completed
    // fire before ws->stop() in the common case.
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(body, "okok");
    curl_easy_cleanup(curl);

    // Sanity: producer ran at least twice (one chunk + EOS).
    LT_CHECK(producer_calls.load() >= 2);

    // Force MHD to fire request_completed for every pending connection.
    // MHD_stop_daemon (called by stop()) joins internal threads and drains
    // the request_completed queue. tear_down() will call stop() again after
    // this test body returns; that is safe because webserver::stop() is
    // idempotent — the second call is a documented no-op.
    ws->stop();

    // Wait for the sentinel to be destroyed. request_completed may fire on
    // a worker thread that finishes a hair after stop() returns, so we use
    // a condition variable rather than a busy-poll. 5-second upper bound
    // catches a genuine DR-010 regression without hanging CI indefinitely.
    {
        std::unique_lock<std::mutex> lk(destroyed_mu);
        destroyed_cv.wait_for(lk, std::chrono::seconds(5),
                              [&destroyed] { return destroyed.load(); });
    }
    LT_CHECK_EQ(destroyed.load(), true);
LT_END_AUTO_TEST(deferred_producer_destroyed_in_request_completed)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
