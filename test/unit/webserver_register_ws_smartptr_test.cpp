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

// TASK-035: smart-pointer register_ws_resource overloads.
//
// This TU pins both the compile-time signature contract for the new
// register_ws_resource overloads (mirrors TASK-023's register_resource
// pattern) and the runtime ownership semantics (HAVE_WEBSOCKET-on only):
//   - unique_ptr overload: webserver takes ownership; handler dtor runs
//     when the webserver is destroyed.
//   - shared_ptr overload: caller retains a reference; handler lives as
//     long as either the webserver or the caller holds it.
//   - Null smart-pointer arguments throw std::invalid_argument.
//   - Duplicate registrations throw std::invalid_argument (mirroring
//     TASK-023's throw-on-duplicate; v1 silently overwrote).
//   - unregister_ws_resource drops the slot and a subsequent
//     re-registration succeeds.
//
// On a HAVE_WEBSOCKET-off build the negative throw-site contract is
// owned by webserver_ws_unavailable_test.cpp; this TU's runtime tests
// are guarded by #ifdef HAVE_WEBSOCKET. The compile-time signature
// contract is always exercised (it touches the public surface, which is
// unconditional per TASK-034 / PRD-FLG-REQ-001).

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::webserver;
using httpserver::websocket_handler;

#define PORT 8095

// ---- Compile-time signature contract -----------------------------------
//
// Probe overloaded members through `decltype` of a call expression: a
// well-formed call means the overload exists with the given argument
// shape. Mirrors the TASK-023 layout in webserver_register_smartptr_test.

// (1) unique_ptr overload exists and returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_ws_resource(
                      std::declval<const std::string&>(),
                      std::declval<std::unique_ptr<websocket_handler>>())),
                  void>,
              "register_ws_resource(const string&, unique_ptr<websocket_handler>) "
              "must exist and return void");

// (2) shared_ptr overload exists and returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().register_ws_resource(
                      std::declval<const std::string&>(),
                      std::declval<std::shared_ptr<websocket_handler>>())),
                  void>,
              "register_ws_resource(const string&, shared_ptr<websocket_handler>) "
              "must exist and return void");

// (3) unregister_ws_resource exists and returns void.
static_assert(std::is_same_v<
                  decltype(std::declval<webserver&>().unregister_ws_resource(
                      std::declval<const std::string&>())),
                  void>,
              "unregister_ws_resource(const string&) must exist and return void");

// (4) Negative: the raw-pointer overload must be gone. SFINAE void_t
//     trick: if the call expression is well-formed for any overload,
//     the partial specialization selects and ::value flips to true.
template <typename, typename = void>
struct has_raw_register_ws_resource : std::false_type {};

template <typename WS>
struct has_raw_register_ws_resource<WS, std::void_t<
    decltype(std::declval<WS&>().register_ws_resource(
        std::declval<const std::string&>(),
        std::declval<websocket_handler*>()))>> : std::true_type {};

static_assert(!has_raw_register_ws_resource<webserver>::value,
              "the raw-pointer register_ws_resource overload must be removed");

// ---- Runtime ownership tests (HAVE_WEBSOCKET-on only) -----------------

#ifdef HAVE_WEBSOCKET

namespace {

// websocket_handler subclass with a static dtor counter, so tests can
// observe ownership-driven destruction. The handler hook overrides are
// no-ops -- the tests never actually drive a WebSocket session.
class counted_ws_handler : public websocket_handler {
 public:
     static std::atomic<int> dtor_count;

     counted_ws_handler() = default;
     ~counted_ws_handler() override { ++dtor_count; }
};

std::atomic<int> counted_ws_handler::dtor_count{0};

// Trivial websocket_handler subclass used by the "compiles" acceptance
// test. The base class's default virtual hooks suffice: the test only
// asserts the registration call compiles and does not throw.
class my_ws_handler : public websocket_handler {};

}  // namespace

LT_BEGIN_SUITE(webserver_register_ws_smartptr_suite)
    void set_up() {
        counted_ws_handler::dtor_count = 0;
    }

    void tear_down() {}
LT_END_SUITE(webserver_register_ws_smartptr_suite)

// Acceptance criterion (verbatim from TASK-035 spec):
//   "auto h = std::make_unique<my_ws_handler>();
//    ws.register_ws_resource('/ws', std::move(h));
//    compiles and serves WebSocket frames."
//
// The "serves WebSocket frames" half is covered by the integ-level
// ws_start_stop test; here we only assert the registration call
// succeeds on the public-API level (no throw).
LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   unique_ptr_overload_compiles)
    webserver ws{create_webserver(PORT)};
    auto h = std::make_unique<my_ws_handler>();
    // Must compile, must not throw on a HAVE_WEBSOCKET-on build.
    ws.register_ws_resource("/ws", std::move(h));
LT_END_AUTO_TEST(unique_ptr_overload_compiles)

// Acceptance: webserver destruction runs the handler's dtor when the
// caller transferred ownership via unique_ptr (mirrors TASK-023
// `unique_ptr_dtor_runs_on_webserver_destruction`).
LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   unique_ptr_dtor_runs_on_webserver_destruction)
    LT_CHECK_EQ(counted_ws_handler::dtor_count.load(), 0);
    {
        webserver ws{create_webserver(PORT + 1)};
        ws.register_ws_resource("/ws", std::make_unique<counted_ws_handler>());
        // No start/stop needed -- registration alone must transfer
        // ownership; webserver destruction must run the dtor.
    }
    LT_CHECK_EQ(counted_ws_handler::dtor_count.load(), 1);
LT_END_AUTO_TEST(unique_ptr_dtor_runs_on_webserver_destruction)

// shared_ptr semantics: caller's shared_ptr keeps the handler alive
// past webserver destruction. The dtor runs only once both refs drop.
LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   shared_ptr_caller_keeps_handler_alive)
    LT_CHECK_EQ(counted_ws_handler::dtor_count.load(), 0);
    auto sp = std::make_shared<counted_ws_handler>();
    {
        webserver ws{create_webserver(PORT + 2)};
        ws.register_ws_resource("/ws", sp);
    }
    // Webserver destroyed; caller still holds a ref.
    LT_CHECK_EQ(counted_ws_handler::dtor_count.load(), 0);
    sp.reset();
    LT_CHECK_EQ(counted_ws_handler::dtor_count.load(), 1);
LT_END_AUTO_TEST(shared_ptr_caller_keeps_handler_alive)

LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   null_unique_ptr_throws)
    webserver ws{create_webserver(PORT + 3)};
    bool threw = false;
    try {
        ws.register_ws_resource("/ws", std::unique_ptr<websocket_handler>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(null_unique_ptr_throws)

LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   null_shared_ptr_throws)
    webserver ws{create_webserver(PORT + 4)};
    bool threw = false;
    try {
        ws.register_ws_resource("/ws", std::shared_ptr<websocket_handler>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(null_shared_ptr_throws)

// Mirror TASK-023's duplicate-registration throw. v1's
// register_ws_resource silently overwrote (operator[] semantics);
// v2.0 throws to match the rest of the registration surface.
LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   duplicate_registration_throws)
    webserver ws{create_webserver(PORT + 5)};
    ws.register_ws_resource("/dup", std::make_shared<my_ws_handler>());
    bool threw = false;
    try {
        ws.register_ws_resource("/dup", std::make_shared<my_ws_handler>());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, true);
LT_END_AUTO_TEST(duplicate_registration_throws)

// unregister drops the slot; a re-registration on the same path then
// succeeds without throwing. Mirrors unregister_path semantics.
LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   unregister_ws_resource_drops_handler)
    webserver ws{create_webserver(PORT + 6)};
    ws.register_ws_resource("/x", std::make_shared<my_ws_handler>());
    ws.unregister_ws_resource("/x");
    // Slot is empty; re-registering must not throw.
    bool threw = false;
    try {
        ws.register_ws_resource("/x", std::make_shared<my_ws_handler>());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(unregister_ws_resource_drops_handler)

// Unregistering an unknown path is a no-op (mirrors unregister_path /
// unregister_resource semantics at src/webserver.cpp:1122).
LT_BEGIN_AUTO_TEST(webserver_register_ws_smartptr_suite,
                   unregister_missing_ws_resource_is_noop)
    webserver ws{create_webserver(PORT + 7)};
    bool threw = false;
    try {
        ws.unregister_ws_resource("/never-registered");
    } catch (...) {
        threw = true;
    }
    LT_CHECK_EQ(threw, false);
LT_END_AUTO_TEST(unregister_missing_ws_resource_is_noop)

#endif  // HAVE_WEBSOCKET

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
