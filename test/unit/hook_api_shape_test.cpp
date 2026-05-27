/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-045: API shape + runtime semantics for the hook bus skeleton.
//
// Compile-time:
//   - hook_phase is an enum with count_ == 11.
//   - hook_action is move-only.
//   - hook_handle is default-constructible and move-only.
//   - webserver::add_hook(phase, std::function<...>) returns hook_handle.
//
// Runtime (against a non-started webserver — no MHD daemon needed):
//   - Registering a hook flips any_hooks_[phase] true; explicit remove()
//     flips it back and clears the phase vector.
//   - Double-remove is a no-op (idempotent).
//   - RAII destruction of a non-detached handle removes the entry.
//   - detach() disarms the destructor.

#include <functional>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

using httpserver::create_webserver;
using httpserver::hook_action;
using httpserver::hook_handle;
using httpserver::hook_phase;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::webserver;
using httpserver::detail::webserver_impl;

// ---- compile-time assertions --------------------------------------------

static_assert(std::is_enum_v<hook_phase>, "hook_phase must be an enum");
static_assert(static_cast<std::size_t>(hook_phase::count_) == 11u,
              "hook_phase::count_ must be 11");

static_assert(!std::is_copy_constructible_v<hook_action>,
              "hook_action must be move-only");
static_assert(!std::is_copy_assignable_v<hook_action>,
              "hook_action must be move-only");
static_assert(std::is_nothrow_move_constructible_v<hook_action>,
              "hook_action must be nothrow move-constructible");
static_assert(std::is_nothrow_move_assignable_v<hook_action>,
              "hook_action must be nothrow move-assignable");

static_assert(std::is_default_constructible_v<hook_handle>,
              "hook_handle must be default-constructible");
static_assert(!std::is_copy_constructible_v<hook_handle>,
              "hook_handle must be move-only");
static_assert(!std::is_copy_assignable_v<hook_handle>,
              "hook_handle must be move-only");
static_assert(std::is_nothrow_move_constructible_v<hook_handle>,
              "hook_handle must be nothrow move-constructible");
static_assert(std::is_nothrow_move_assignable_v<hook_handle>,
              "hook_handle must be nothrow move-assignable");

// D11 — ABI size pin so future growth is reviewed explicitly.
// TASK-051 bumped this from 32 to 48 to make room for the per-route
// path's weak_ptr<detail::resource_hook_table> member. Layout (libc++):
//   impl_       (8) + slot_id_ (8) + phase_ (1) + armed_ (1) + pad (6)
//   + table_weak_ (16)
// = 40, padded to 48.
static_assert(sizeof(hook_handle) <= 48,
              "hook_handle size grew unexpectedly past 48 bytes");

// add_hook returns hook_handle, taking a typed std::function.
static_assert(std::is_same_v<
        decltype(std::declval<webserver&>().add_hook(
            hook_phase::response_sent,
            std::function<void(const httpserver::response_sent_ctx&)>{})),
        hook_handle>,
    "webserver::add_hook(response_sent, ...) must return hook_handle");

// Negative compile gate: a callable whose signature does not match
// ANY of the eleven add_hook overloads must NOT be invocable as
// add_hook. This pins the "wrong signature fails to compile"
// acceptance criterion via SFINAE rather than a should-fail TU.
template <class FnT, class = void>
struct add_hook_is_callable : std::false_type {};
template <class FnT>
struct add_hook_is_callable<FnT, std::void_t<
    decltype(std::declval<webserver&>().add_hook(
        std::declval<hook_phase>(), std::declval<FnT>()))>>
    : std::true_type {};

// A callable returning int -- not one of the eleven signatures.
static_assert(!add_hook_is_callable<std::function<int(int)>>::value,
    "add_hook must reject callables that do not match any phase signature");

// A callable taking a raw `int&` -- not one of the eleven context types.
static_assert(!add_hook_is_callable<std::function<void(int&)>>::value,
    "add_hook must reject callables that take a non-context type");

// Positive control: at least one of the eleven valid signatures IS
// callable. (Catches accidental drift that would silently disable
// add_hook entirely.)
static_assert(add_hook_is_callable<
        std::function<void(const httpserver::response_sent_ctx&)>>::value,
    "add_hook must accept the response_sent signature");

// ---- runtime tests ------------------------------------------------------

namespace {

webserver make_ws() {
    return webserver{create_webserver(8197)};
}

// Probe helpers — read the impl's per-phase vector size / any_hooks_ flag
// through the HTTPSERVER_COMPILATION-gated friend bridge. The dispatch
// is keyed by phase, so we centralise the switch here.
std::size_t phase_size(webserver_impl* impl, hook_phase p);
bool any_hook(webserver_impl* impl, hook_phase p) {
    return impl->any_hooks_[static_cast<std::size_t>(p)].load(
        std::memory_order_acquire);
}

}  // namespace

LT_BEGIN_SUITE(hook_api_shape_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(hook_api_shape_suite)

LT_BEGIN_AUTO_TEST(hook_api_shape_suite, hook_action_pass_and_respond_with)
    hook_action a;  // default == pass()
    LT_CHECK_EQ(a.is_pass(), true);

    hook_action b = hook_action::respond_with(
        http_response::string("hello"));
    LT_CHECK_EQ(b.is_pass(), false);

    http_response r = std::move(b).take_response();
    (void)r;  // moved-out token; not used further
LT_END_AUTO_TEST(hook_action_pass_and_respond_with)

LT_BEGIN_AUTO_TEST(hook_api_shape_suite, add_then_explicit_remove)
    webserver ws = make_ws();
    auto* impl = httpserver::webserver_test_access::impl(ws);

    LT_CHECK_EQ(any_hook(impl, hook_phase::response_sent), false);
    LT_CHECK_EQ(phase_size(impl, hook_phase::response_sent),
                static_cast<std::size_t>(0));

    auto h = ws.add_hook(hook_phase::response_sent,
        std::function<void(const httpserver::response_sent_ctx&)>(
            [](const httpserver::response_sent_ctx&) {}));

    LT_CHECK_EQ(any_hook(impl, hook_phase::response_sent), true);
    LT_CHECK_EQ(phase_size(impl, hook_phase::response_sent),
                static_cast<std::size_t>(1));

    h.remove();
    LT_CHECK_EQ(any_hook(impl, hook_phase::response_sent), false);
    LT_CHECK_EQ(phase_size(impl, hook_phase::response_sent),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(add_then_explicit_remove)

LT_BEGIN_AUTO_TEST(hook_api_shape_suite, double_remove_is_noop)
    webserver ws = make_ws();
    auto h = ws.add_hook(hook_phase::after_handler,
        std::function<hook_action(httpserver::after_handler_ctx&)>(
            [](httpserver::after_handler_ctx&) { return hook_action{}; }));
    h.remove();
    h.remove();  // must not crash, must not double-erase
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(double_remove_is_noop)

LT_BEGIN_AUTO_TEST(hook_api_shape_suite, raii_destruction_removes)
    webserver ws = make_ws();
    auto* impl = httpserver::webserver_test_access::impl(ws);
    {
        auto h = ws.add_hook(hook_phase::request_completed,
            std::function<void(const httpserver::request_completed_ctx&)>(
                [](const httpserver::request_completed_ctx&) {}));
        LT_CHECK_EQ(phase_size(impl, hook_phase::request_completed),
                    static_cast<std::size_t>(1));
    }
    // ~hook_handle() should have re-taken the lock and erased the slot.
    LT_CHECK_EQ(phase_size(impl, hook_phase::request_completed),
                static_cast<std::size_t>(0));
    LT_CHECK_EQ(any_hook(impl, hook_phase::request_completed), false);
LT_END_AUTO_TEST(raii_destruction_removes)

LT_BEGIN_AUTO_TEST(hook_api_shape_suite, detach_disarms_destructor)
    webserver ws = make_ws();
    auto* impl = httpserver::webserver_test_access::impl(ws);
    {
        auto h = ws.add_hook(hook_phase::connection_opened,
            std::function<void(const httpserver::connection_open_ctx&)>(
                [](const httpserver::connection_open_ctx&) {}));
        std::move(h).detach();
        // Local h is now in the disarmed (none) state; dtor must NOT
        // re-take the lock or erase the slot.
    }
    LT_CHECK_EQ(phase_size(impl, hook_phase::connection_opened),
                static_cast<std::size_t>(1));
    LT_CHECK_EQ(any_hook(impl, hook_phase::connection_opened), true);
LT_END_AUTO_TEST(detach_disarms_destructor)

LT_BEGIN_AUTO_TEST(hook_api_shape_suite, any_hooks_flips_on_first_and_last)
    webserver ws = make_ws();
    auto* impl = httpserver::webserver_test_access::impl(ws);

    LT_CHECK_EQ(any_hook(impl, hook_phase::before_handler), false);

    auto h1 = ws.add_hook(hook_phase::before_handler,
        std::function<hook_action(httpserver::before_handler_ctx&)>(
            [](httpserver::before_handler_ctx&) { return hook_action{}; }));
    LT_CHECK_EQ(any_hook(impl, hook_phase::before_handler), true);

    auto h2 = ws.add_hook(hook_phase::before_handler,
        std::function<hook_action(httpserver::before_handler_ctx&)>(
            [](httpserver::before_handler_ctx&) { return hook_action{}; }));
    LT_CHECK_EQ(any_hook(impl, hook_phase::before_handler), true);

    h1.remove();
    // One hook still on this phase — gate must remain true.
    LT_CHECK_EQ(any_hook(impl, hook_phase::before_handler), true);

    h2.remove();
    // Last hook removed — gate must flip back to false.
    LT_CHECK_EQ(any_hook(impl, hook_phase::before_handler), false);
LT_END_AUTO_TEST(any_hooks_flips_on_first_and_last)

LT_BEGIN_AUTO_TEST(hook_api_shape_suite, default_constructed_handle_is_inert)
    hook_handle h;
    h.remove();  // no-op
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(default_constructed_handle_is_inert)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()

// ---- phase_size helper (out-of-line so the switch lives in one place) ---
namespace {

std::size_t phase_size(webserver_impl* impl, hook_phase p) {
    switch (p) {
    case hook_phase::connection_opened:
        return impl->hooks_connection_opened_.size();
    case hook_phase::accept_decision:
        return impl->hooks_accept_decision_.size();
    case hook_phase::request_received:
        return impl->hooks_request_received_.size();
    case hook_phase::body_chunk:
        return impl->hooks_body_chunk_.size();
    case hook_phase::route_resolved:
        return impl->hooks_route_resolved_.size();
    case hook_phase::before_handler:
        return impl->hooks_before_handler_.size();
    case hook_phase::handler_exception:
        return impl->hooks_handler_exception_.size();
    case hook_phase::after_handler:
        return impl->hooks_after_handler_.size();
    case hook_phase::response_sent:
        return impl->hooks_response_sent_.size();
    case hook_phase::request_completed:
        return impl->hooks_request_completed_.size();
    case hook_phase::connection_closed:
        return impl->hooks_connection_closed_.size();
    case hook_phase::count_:
        return 0;
    }
    return 0;
}

}  // namespace
