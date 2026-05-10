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

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::webserver;

#define PORT 8080

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

class ok_resource : public http_resource {
 public:
     std::shared_ptr<http_response> render_get(const http_request&) override {
         return std::make_shared<http_response>(http_response::string("OK"));
     }
};

// Resource whose destructor increments a static counter, so tests can
// observe ownership-driven destruction.
class counted_resource : public http_resource {
 public:
     static std::atomic<int> dtor_count;

     counted_resource() = default;
     ~counted_resource() override { ++dtor_count; }

     std::shared_ptr<http_response> render_get(const http_request&) override {
         return std::make_shared<http_response>(http_response::string("OK"));
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

// (3) The bool-family parameter remains on both overloads so TASK-024
//     can do the register_path/register_prefix split in one go without
//     re-touching every call site twice.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_resource(
                      std::declval<const std::string&>(),
                      std::declval<std::unique_ptr<http_resource>>(),
                      true)),
                  void>,
              "unique_ptr overload must accept a trailing bool family arg");
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_resource(
                      std::declval<const std::string&>(),
                      std::declval<std::shared_ptr<http_resource>>(),
                      true)),
                  void>,
              "shared_ptr overload must accept a trailing bool family arg");

// (4) Negative: the raw-pointer overload must be gone. Use a SFINAE
//     helper so this is observable as a compile-time bool.
template <typename = void>
struct has_raw_register_resource : std::false_type {};

template <>
struct has_raw_register_resource<std::void_t<
    decltype(std::declval<webserver&>().register_resource(
        std::declval<const std::string&>(),
        std::declval<http_resource*>()))>> : std::true_type {};

static_assert(!has_raw_register_resource<>::value,
              "the raw-pointer register_resource overload must be removed");

// ---- Runtime ownership tests ------------------------------------------

LT_BEGIN_SUITE(webserver_register_smartptr_suite)
    void set_up() {
        counted_resource::dtor_count = 0;
    }

    void tear_down() {}
LT_END_SUITE(webserver_register_smartptr_suite)

// Acceptance criterion (verbatim from TASK-023 spec):
//   "auto r = std::make_unique<my_resource>();
//    ws.register_resource('/foo', std::move(r)); compiles and serves."
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   unique_ptr_overload_compiles_and_serves)
    webserver ws = create_webserver(PORT);
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
    ws.stop();
LT_END_AUTO_TEST(unique_ptr_overload_compiles_and_serves)

// Acceptance criterion: "A test verifies the resource destructor runs
// when the webserver is destroyed."
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   unique_ptr_dtor_runs_on_webserver_destruction)
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 0);
    {
        webserver ws = create_webserver(PORT + 1);
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
        webserver ws = create_webserver(PORT + 2);
        ws.register_resource("/x", sp);
    }
    // Webserver destroyed; caller still holds a ref.
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 0);
    sp.reset();
    LT_CHECK_EQ(counted_resource::dtor_count.load(), 1);
LT_END_AUTO_TEST(shared_ptr_caller_keeps_resource_alive)

LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   null_unique_ptr_throws)
    webserver ws = create_webserver(PORT + 3);
    bool threw = false;
    try {
        ws.register_resource("/x", std::unique_ptr<http_resource>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(null_unique_ptr_throws)

LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   null_shared_ptr_throws)
    webserver ws = create_webserver(PORT + 4);
    bool threw = false;
    try {
        ws.register_resource("/x", std::shared_ptr<http_resource>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(null_shared_ptr_throws)

// New void-returning API replaces v1's silent `return false` on
// duplicate registration with a throw, matching the existing
// throw-on-null behavior.
LT_BEGIN_AUTO_TEST(webserver_register_smartptr_suite,
                   duplicate_registration_throws)
    webserver ws = create_webserver(PORT + 5);
    ws.register_resource("/dup", std::make_shared<ok_resource>());
    bool threw = false;
    try {
        ws.register_resource("/dup", std::make_shared<ok_resource>());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(duplicate_registration_throws)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
