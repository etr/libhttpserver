/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// Design pin: the internal_error_handler alias lives in a
// dedicated single-slot member (handler_exception_alias_) on
// webserver_impl, NOT in the hooks_handler_exception_ vector.
//
// Why a separate slot and not a vector entry (like the other v1 aliases)?
// Because handler_exception is the one phase whose alias must fire LAST
// in the chain: user-added handler_exception hooks via
// add_hook get a chance to recover BEFORE the internal_error_handler
// fallback. Placing the alias at index 0 in the vector (the
// convention for first-fire aliases) would invert that ordering. Hence
// the dedicated last-position slot.
//
// This test pins three structural invariants:
//   1. A webserver built without internal_error_handler -> slot is empty
//      AND the user vector is size 0.
//   2. A webserver built WITH internal_error_handler(fn) -> slot is
//      non-empty AND the user vector is still size 0 (no +1 entry, unlike
//      the v1 aliases which DO push +1).
//   3. ws.add_hook(handler_exception, fn) appends to the user vector and
//      leaves the alias slot independent.
//
// Note: the tests reach into
// webserver_impl::handler_exception_alias_ and hooks_handler_exception_
// directly via the webserver_test_access friend bridge. This coupling is
// intentional and is the established project-wide pattern for pinning
// structural invariants (the last-position slot). If the
// implementation is refactored to rename or merge these fields, ALL three
// sub-tests below must be updated in lockstep with the implementation.
// Port 0 is used (no start() is ever called, so OS port assignment is
// irrelevant) to avoid any potential bind-collision.

#include <functional>
#include <string_view>

#include "./httpserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::handler_exception_ctx;
using httpserver::hook_action;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;
using httpserver::detail::webserver_impl;

namespace {

webserver_impl* impl_of(webserver& ws) {
    return httpserver::webserver_test_access::impl(ws);
}

}  // namespace

LT_BEGIN_SUITE(hooks_handler_exception_slot_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_handler_exception_slot_suite)

LT_BEGIN_AUTO_TEST(hooks_handler_exception_slot_suite,
                   baseline_no_alias_slot_empty_and_vector_empty)
    webserver ws{create_webserver(0)};
    auto* impl = impl_of(ws);
    LT_CHECK(!impl->hooks_.handler_exception_alias_);
    LT_CHECK_EQ(impl->hooks_.hooks_handler_exception_.size(),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(baseline_no_alias_slot_empty_and_vector_empty)

LT_BEGIN_AUTO_TEST(hooks_handler_exception_slot_suite,
                   internal_error_handler_populates_alias_slot_not_vector)
    auto handler = [](const http_request&,
                      std::string_view) -> http_response {
        return http_response::string("500");
    };
    webserver ws{create_webserver(0)
        .internal_error_handler(handler)};
    auto* impl = impl_of(ws);
    LT_CHECK(static_cast<bool>(impl->hooks_.handler_exception_alias_));
    // The alias must NOT push an entry into the user vector -- this is
    // the design contract distinguishing handler_exception from the
    // v1 aliases.
    LT_CHECK_EQ(impl->hooks_.hooks_handler_exception_.size(),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(internal_error_handler_populates_alias_slot_not_vector)

LT_BEGIN_AUTO_TEST(hooks_handler_exception_slot_suite,
                   user_add_hook_grows_vector_alias_slot_untouched)
    auto handler = [](const http_request&,
                      std::string_view) -> http_response {
        return http_response::string("500");
    };
    webserver ws{create_webserver(0)
        .internal_error_handler(handler)};
    auto* impl = impl_of(ws);

    LT_CHECK(static_cast<bool>(impl->hooks_.handler_exception_alias_));
    LT_CHECK_EQ(impl->hooks_.hooks_handler_exception_.size(),
                static_cast<std::size_t>(0));

    auto h = ws.add_hook(hook_phase::handler_exception,
        std::function<hook_action(const handler_exception_ctx&)>(
            [](const handler_exception_ctx&) {
                return hook_action::pass();
            }));

    // The user vector grew by 1; the alias slot is still set independently.
    LT_CHECK(static_cast<bool>(impl->hooks_.handler_exception_alias_));
    LT_CHECK_EQ(impl->hooks_.hooks_handler_exception_.size(),
                static_cast<std::size_t>(1));
LT_END_AUTO_TEST(user_add_hook_grows_vector_alias_slot_untouched)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
