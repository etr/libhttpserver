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

// TASK-023: smart-pointer register_resource overloads.
//
// This TU pins both the compile-time signature contract for the new
// register_resource overloads and the runtime ownership semantics:
//   - unique_ptr overload: webserver takes ownership; resource dtor
//     runs when the webserver is destroyed (acceptance criterion).
//   - shared_ptr overload: caller retains a reference; resource lives
//     as long as either the webserver or the caller holds it.
//   - Null smart-pointer arguments throw std::invalid_argument.
//   - Duplicate registrations throw std::invalid_argument (replacing
//     the v1 "return false" silent-fail behavior, which is gone with
//     the new void return type).

#include <curl/curl.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"
#include "./littletest.hpp"

// TASK-024: register_resource is now [[deprecated]] in favor of
// register_path / register_prefix. This TU continues to exercise the
// deprecated forwarder so its ownership semantics stay verified;
// suppress the file-wide deprecation warning so -Werror still passes.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

// PORT and the PORT+N offsets below are fixed numeric values shared by all
// tests in this TU. The tests run sequentially (single-process), so the
// offsets (PORT+0 through PORT+5) do not conflict with each other.
// If a future parallel test runner binds the same ports concurrently, switch
// to create_webserver(0) and retrieve the OS-assigned port via
// ws.get_bound_port() after ws.start().
#define PORT 8080

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

// Resource whose destructor increments a static counter, so tests can
// observe ownership-driven destruction.
class counted_resource : public http_resource {
 public:
     static std::atomic<int> dtor_count;

     counted_resource() = default;
     ~counted_resource() override { ++dtor_count; }

     http_response render_get(const http_request&) override {
         return http_response::string("OK");
     }
};

std::atomic<int> counted_resource::dtor_count{0};

}  // namespace

// ---- Compile-time signature contract -----------------------------------
//
// Probing overloaded members through `decltype` of a call expression: a
// well-formed call means the overload exists with the given argument
// shape; an ill-formed expression would fail SFINAE inside the
// surrounding decltype. We exercise each overload through its natural
// call syntax.

// (1) unique_ptr overload exists and returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_resource(
                      std::declval<const std::string&>(),
                      std::declval<std::unique_ptr<http_resource>>())),
                  void>,
              "register_resource(const string&, unique_ptr<http_resource>) "
              "must exist and return void");

// (2) shared_ptr overload exists and returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_resource(
                      std::declval<const std::string&>(),
                      std::declval<std::shared_ptr<http_resource>>())),
                  void>,
              "register_resource(const string&, shared_ptr<http_resource>) "
              "must exist and return void");

// (3) TASK-024 removed the trailing `bool family` parameter from both
//     overloads. The bool-family overload is now pinned absent by the
//     negative SFINAE in webserver_register_path_prefix_test.cpp; users
//     that want prefix matching must call register_prefix() directly.

// (4) Negative: the raw-pointer overload must be gone. SFINAE template
//     specialization on a void_t of the call expression — if the call
//     is well-formed for any overload, the partial specialization
//     selects and ::value flips to true.
template <typename, typename = void>
struct has_raw_register_resource : std::false_type {};

template <typename WS>
struct has_raw_register_resource<WS, std::void_t<
    decltype(std::declval<WS&>().register_resource(
        std::declval<const std::string&>(),
        std::declval<http_resource*>()))>> : std::true_type {};

static_assert(!has_raw_register_resource<webserver>::value,
              "the raw-pointer register_resource overload must be removed");

// ---- Runtime ownership tests ------------------------------------------

LT_BEGIN_SUITE(webserver_register_smartptr_suite)
    void set_up() {
        // Reset the static dtor_count before every test so tests do not
        // accumulate each other's destructor calls. This relies on the
        // current runner executing tests sequentially; if a parallel
        // runner is ever introduced, replace dtor_count with a per-test
        // local atomic passed by reference.
        counted_resource::dtor_count = 0;
    }

    void tear_down() {}
LT_END_SUITE(webserver_register_smartptr_suite)

// Acceptance criterion (verbatim from TASK-023 spec):
//   "auto r = std::make_unique<my_resource>();
//    ws.register_resource('/foo', std::move(r)); compiles and serves."
//
// Note: the static_asserts above (lines 101-137) already verify the
// compile-time contract. This test adds the runtime "serves" signal via a
// real curl round-trip, making it effectively an integration test embedded
// in this unit TU. The network I/O is intentional — it satisfies the spec's
// explicit "compiles AND serves" requirement.
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   unique_ptr_overload_compiles_and_serves)
    webserver ws{create_webserver(PORT)};
    auto r = std::make_unique<ok_resource>();
    ws.register_resource("/foo", std::move(r));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/foo");
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
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 0);
    {
        webserver ws{create_webserver(PORT + 1)};
        ws.register_resource("/x", std::make_unique<counted_resource>());
        // No start/stop needed — registration alone must transfer
        // ownership; webserver destruction must run the dtor.
    }
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 1);
LT_END_AUTO_TEST(unique_ptr_dtor_runs_on_webserver_destruction)

// shared_ptr semantics: caller's shared_ptr keeps the resource alive
// past webserver destruction. The dtor runs only once both refs drop.
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   shared_ptr_caller_keeps_resource_alive)
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 0);
    auto sp = std::make_shared<counted_resource>();
    {
        webserver ws{create_webserver(PORT + 2)};
        ws.register_resource("/x", sp);
    }
    // Webserver destroyed; caller still holds a ref.
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 0);
    sp.reset();
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 1);
LT_END_AUTO_TEST(shared_ptr_caller_keeps_resource_alive)

LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   null_unique_ptr_throws)
    webserver ws{create_webserver(PORT + 3)};
    bool caught_invalid_argument = false;
    try {
        ws.register_resource("/x", std::unique_ptr<http_resource>{});
    } catch (const std::invalid_argument&) {
        caught_invalid_argument = true;
    }
    // Assert after the try-catch so a spurious exception inside the catch
    // branch cannot mask a missed throw by leaving the flag unset.
    LT_CHECK_EQ(caught_invalid_argument, true);
LT_END_AUTO_TEST(null_unique_ptr_throws)

LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   null_shared_ptr_throws)
    webserver ws{create_webserver(PORT + 4)};
    bool caught_invalid_argument = false;
    try {
        ws.register_resource("/x", std::shared_ptr<http_resource>{});
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
    webserver ws{create_webserver(PORT + 5)};
    ws.register_resource("/dup", std::make_shared<ok_resource>());
    bool caught_invalid_argument = false;
    try {
        ws.register_resource("/dup", std::make_shared<ok_resource>());
    } catch (const std::invalid_argument&) {
        caught_invalid_argument = true;
    }
    LT_CHECK_EQ(caught_invalid_argument, true);
LT_END_AUTO_TEST(duplicate_registration_throws)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
