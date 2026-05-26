/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-050: design pin for the log_access alias.
//
// log_access(fn) is documented as a response_sent alias. Unlike the
// TASK-048 v1 aliases (auth/404/405) which push +1 into the user vector,
// the log_access alias lives in a dedicated single-slot member on
// webserver_impl (log_access_alias_), mirroring the TASK-049
// internal_error_handler alias slot. The fire site invokes the user
// vector first, then the alias slot.
//
// Why a slot and not a vector entry? Two reasons:
//   1. The slot lets user-added response_sent hooks observe the response
//      BEFORE the legacy access log formats it, preserving the v1
//      "logger sees the trailing event" contract.
//   2. Re-registration semantics ("replaces" per the task spec) are
//      naturally expressed as a single-writer slot rather than a
//      multi-entry vector. (At v2.0 the writer is webserver construction
//      only; a future runtime setter would still write the same slot.)
//
// This test pins three structural invariants:
//   1. A webserver built without log_access -> slot is empty AND the
//      response_sent vector is size 0.
//   2. A webserver built WITH log_access(fn) -> slot is non-empty AND
//      the response_sent vector is still size 0 (no +1 entry, unlike
//      the TASK-048 aliases which DO push +1).
//   3. ws.add_hook(response_sent, fn) appends to the user vector and
//      leaves the alias slot independent.

#include <functional>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::hook_phase;
using httpserver::response_sent_ctx;
using httpserver::webserver;
using httpserver::detail::webserver_impl;

namespace {

webserver_impl* impl_of(webserver& ws) {
    return httpserver::webserver_test_access::impl(ws);
}

}  // namespace

LT_BEGIN_SUITE(hooks_log_access_alias_slot_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_log_access_alias_slot_suite)

LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   baseline_no_alias_slot_empty_and_vector_empty)
    webserver ws{create_webserver(8244)};
    auto* impl = impl_of(ws);
    LT_CHECK(!impl->log_access_alias_);
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(baseline_no_alias_slot_empty_and_vector_empty)

LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   log_access_populates_alias_slot_not_vector)
    auto logger = [](const std::string&) {};
    webserver ws{create_webserver(8244)
        .log_access(logger)};
    auto* impl = impl_of(ws);
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    // The alias must NOT push an entry into the user vector.
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(log_access_populates_alias_slot_not_vector)

LT_BEGIN_AUTO_TEST(hooks_log_access_alias_slot_suite,
                   user_add_hook_grows_vector_alias_slot_untouched)
    auto logger = [](const std::string&) {};
    webserver ws{create_webserver(8244)
        .log_access(logger)};
    auto* impl = impl_of(ws);

    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(0));

    auto h = ws.add_hook(hook_phase::response_sent,
        std::function<void(const response_sent_ctx&)>(
            [](const response_sent_ctx&) {}));

    // The user vector grew by 1; the alias slot is still set independently.
    LT_CHECK(static_cast<bool>(impl->log_access_alias_));
    LT_CHECK_EQ(impl->hooks_response_sent_.size(),
                static_cast<std::size_t>(1));
LT_END_AUTO_TEST(user_add_hook_grows_vector_alias_slot_untouched)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
