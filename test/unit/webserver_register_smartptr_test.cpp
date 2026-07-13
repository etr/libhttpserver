/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// Smart-pointer register_path ownership semantics.
//
// This TU pins the runtime ownership contract of the register_path
// smart-pointer overloads:
//   - unique_ptr overload: webserver takes ownership; resource dtor
//     runs when the webserver is destroyed (acceptance criterion).
//   - shared_ptr overload: caller retains a reference; resource lives
//     as long as either the webserver or the caller holds it.
//   - Null smart-pointer arguments throw std::invalid_argument.
//   - Duplicate registrations throw std::invalid_argument (replacing
//     the v1 "return false" silent-fail behavior, which is gone with
//     the new void return type).
//
// The compile-time signature contract for register_path (and the
// negative pin that register_resource is removed entirely) lives in
// webserver_register_path_prefix_test.cpp.

#include <curl/curl.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

// TASK-085: this TU is parallel-runner safe.
//   - The one test that starts a server and does a curl round-trip
//     (unique_ptr_overload_compiles_and_serves) binds an OS-assigned
//     ephemeral port via create_webserver(0) and reads it back with
//     ws.get_bound_port() — no fixed port, no cross-test collision.
//   - Every other test constructs create_webserver(0) but never calls
//     start(), so no port is ever bound; the ephemeral 0 is harmless.
//   - The destructor-counting tests use a per-test local std::atomic<int>
//     passed to counted_resource by pointer, not a shared static, so two
//     tests running concurrently cannot contaminate each other's count.

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class ok_resource : public http_resource {
 public:
     http_response render_get(const http_request&) override {
         return http_response::string("OK");
     }
};

// Resource whose destructor increments a caller-supplied counter, so
// tests can observe ownership-driven destruction. The counter is a
// per-test local std::atomic<int> passed by pointer (TASK-085) rather
// than a shared static, so concurrent tests cannot contaminate each
// other's count under a parallel runner.
class counted_resource : public http_resource {
 public:
     explicit counted_resource(std::atomic<int>* counter)
         : counter_(counter) {}
     ~counted_resource() override { ++(*counter_); }

     http_response render_get(const http_request&) override {
         return http_response::string("OK");
     }

 private:
     std::atomic<int>* counter_;
};

}  // namespace

// ---- Runtime ownership tests ------------------------------------------

LT_BEGIN_SUITE(webserver_register_smartptr_suite)
    // No shared mutable state to reset: the destructor-counting tests
    // each own a local std::atomic<int> (TASK-085).
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(webserver_register_smartptr_suite)

// Acceptance criterion (verbatim from TASK-023 spec):
//   "auto r = std::make_unique<my_resource>();
//    ws.register_path('/foo', std::move(r)); compiles and serves."
//
// Note: the compile-time signature contract is verified in
// webserver_register_path_prefix_test.cpp. This test adds the runtime
// "serves" signal via a real curl round-trip, making it effectively an
// integration test embedded in this unit TU. The network I/O is
// intentional — it satisfies the spec's explicit "compiles AND serves"
// requirement.
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   unique_ptr_overload_compiles_and_serves)
    webserver ws{create_webserver(0)};
    auto r = std::make_unique<ok_resource>();
    ws.register_path("/foo", std::move(r));
    ws.start(false);
    // OS-assigned ephemeral port: no fixed port to collide under a
    // parallel runner (TASK-085).
    const std::string url =
        "localhost:" + std::to_string(ws.get_bound_port()) + "/foo";

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, std::string("OK"));
    curl_easy_cleanup(curl);
    curl_global_cleanup();  // mirror curl_global_init to keep state balanced
    ws.stop();
LT_END_AUTO_TEST(unique_ptr_overload_compiles_and_serves)

// Acceptance criterion: "A test verifies the resource destructor runs
// when the webserver is destroyed."
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   unique_ptr_dtor_runs_on_webserver_destruction)
    std::atomic<int> dtor_count{0};
    {
        webserver ws{create_webserver(0)};
        ws.register_path(
            "/x", std::make_unique<counted_resource>(&dtor_count));
        // No start/stop needed — registration alone must transfer
        // ownership; webserver destruction must run the dtor.
    }
    LT_CHECK_EQ(dtor_count.load(), 1);
LT_END_AUTO_TEST(unique_ptr_dtor_runs_on_webserver_destruction)

// shared_ptr semantics: caller's shared_ptr keeps the resource alive
// past webserver destruction. The dtor runs only once both refs drop.
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   shared_ptr_caller_keeps_resource_alive)
    std::atomic<int> dtor_count{0};
    auto sp = std::make_shared<counted_resource>(&dtor_count);
    {
        webserver ws{create_webserver(0)};
        ws.register_path("/x", sp);
    }
    // Webserver destroyed; caller still holds a ref.
    LT_CHECK_EQ(dtor_count.load(), 0);
    sp.reset();
    LT_CHECK_EQ(dtor_count.load(), 1);
LT_END_AUTO_TEST(shared_ptr_caller_keeps_resource_alive)

LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   null_unique_ptr_throws)
    webserver ws{create_webserver(0)};
    bool caught_invalid_argument = false;
    try {
        ws.register_path("/x", std::unique_ptr<http_resource>{});
    } catch (const std::invalid_argument&) {
        caught_invalid_argument = true;
    }
    // Assert after the try-catch so a spurious exception inside the catch
    // branch cannot mask a missed throw by leaving the flag unset.
    LT_CHECK_EQ(caught_invalid_argument, true);
LT_END_AUTO_TEST(null_unique_ptr_throws)

LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   null_shared_ptr_throws)
    webserver ws{create_webserver(0)};
    bool caught_invalid_argument = false;
    try {
        ws.register_path("/x", std::shared_ptr<http_resource>{});
    } catch (const std::invalid_argument&) {
        caught_invalid_argument = true;
    }
    LT_CHECK_EQ(caught_invalid_argument, true);
LT_END_AUTO_TEST(null_shared_ptr_throws)

// New void-returning API replaces v1's silent `return false` on
// duplicate registration with a throw, matching the existing
// throw-on-null behavior.
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   duplicate_registration_throws)
    webserver ws{create_webserver(0)};
    ws.register_path("/dup", std::make_shared<ok_resource>());
    bool caught_invalid_argument = false;
    try {
        ws.register_path("/dup", std::make_shared<ok_resource>());
    } catch (const std::invalid_argument&) {
        caught_invalid_argument = true;
    }
    LT_CHECK_EQ(caught_invalid_argument, true);
LT_END_AUTO_TEST(duplicate_registration_throws)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
