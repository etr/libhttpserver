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

// Unit tests for detail::hook_bus, constructed and driven directly
// (no running webserver/MHD daemon). hook_bus was extracted from
// webserver_impl (commit 1f9a82e) and previously had no test that
// instantiated it in isolation -- its registration validation
// (phase-mismatch / empty-callable throws), the any_hooks_ advisory
// gate arm/clear invariant, the alias-slot wiring, and the firing
// helpers' try/catch-into-log_fn behavior were only reachable through
// slow, full-webserver integration paths (see webserver::add_hook /
// http_resource::add_hook tests). These tests pin that behavior at the
// class boundary.

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "./httpserver.hpp"
#include "./httpserver/detail/hook_bus.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;
namespace htd = httpserver::detail;

LT_BEGIN_SUITE(hook_bus_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hook_bus_suite)

// ----- Registration validation --------------------------------------------

LT_BEGIN_AUTO_TEST(hook_bus_suite, add_wrong_phase_throws_invalid_argument)
    htd::hook_bus bus;
    bool threw = false;
    try {
        // connection_open_ctx's add() overload is implicitly bound to
        // hook_phase::connection_opened; passing accept_decision as the
        // requested tag must trip the phase-mismatch throw in add_impl.
        bus.add(ht::hook_phase::accept_decision,
                std::function<void(const ht::connection_open_ctx&)>(
                    [](const ht::connection_open_ctx&) {}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(add_wrong_phase_throws_invalid_argument)

LT_BEGIN_AUTO_TEST(hook_bus_suite, add_empty_callable_throws_invalid_argument)
    htd::hook_bus bus;
    bool threw = false;
    try {
        bus.add(ht::hook_phase::connection_opened,
                std::function<void(const ht::connection_open_ctx&)>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    LT_CHECK(threw);
LT_END_AUTO_TEST(add_empty_callable_throws_invalid_argument)

// ----- any_hooks_ gate + phase_hook_count ----------------------------------

LT_BEGIN_AUTO_TEST(hook_bus_suite, add_valid_hook_arms_gate_and_counts)
    htd::hook_bus bus;
    LT_CHECK(!bus.has_hooks_for(ht::hook_phase::connection_opened));
    LT_CHECK_EQ(bus.phase_hook_count(ht::hook_phase::connection_opened),
                static_cast<std::size_t>(0));

    const auto slot = bus.add(ht::hook_phase::connection_opened,
        std::function<void(const ht::connection_open_ctx&)>(
            [](const ht::connection_open_ctx&) {}));

    LT_CHECK(slot != 0u);
    LT_CHECK(bus.has_hooks_for(ht::hook_phase::connection_opened));
    LT_CHECK_EQ(bus.phase_hook_count(ht::hook_phase::connection_opened),
                static_cast<std::size_t>(1));
LT_END_AUTO_TEST(add_valid_hook_arms_gate_and_counts)

LT_BEGIN_AUTO_TEST(hook_bus_suite, remove_erases_slot_and_clears_gate)
    htd::hook_bus bus;
    const auto slot = bus.add(ht::hook_phase::connection_closed,
        std::function<void(const ht::connection_close_ctx&)>(
            [](const ht::connection_close_ctx&) {}));
    LT_CHECK(bus.has_hooks_for(ht::hook_phase::connection_closed));

    bus.remove(ht::hook_phase::connection_closed, slot);

    LT_CHECK(!bus.has_hooks_for(ht::hook_phase::connection_closed));
    LT_CHECK_EQ(bus.phase_hook_count(ht::hook_phase::connection_closed),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(remove_erases_slot_and_clears_gate)

// remove() is documented as idempotent: a not-found slot_id (never
// inserted, or already erased) is a silent no-op.
LT_BEGIN_AUTO_TEST(hook_bus_suite, remove_unknown_slot_is_noop)
    htd::hook_bus bus;
    const auto slot = bus.add(ht::hook_phase::connection_closed,
        std::function<void(const ht::connection_close_ctx&)>(
            [](const ht::connection_close_ctx&) {}));

    bus.remove(ht::hook_phase::connection_closed, slot + 999);

    LT_CHECK(bus.has_hooks_for(ht::hook_phase::connection_closed));
    LT_CHECK_EQ(bus.phase_hook_count(ht::hook_phase::connection_closed),
                static_cast<std::size_t>(1));
LT_END_AUTO_TEST(remove_unknown_slot_is_noop)

// ----- Firing: observation-only phases -------------------------------------

LT_BEGIN_AUTO_TEST(hook_bus_suite, fire_connection_opened_invokes_registered_hook)
    htd::hook_bus bus;
    bool fired = false;
    bus.add(ht::hook_phase::connection_opened,
        std::function<void(const ht::connection_open_ctx&)>(
            [&fired](const ht::connection_open_ctx&) { fired = true; }));

    ht::connection_open_ctx ctx{};
    bus.fire_connection_opened(ctx, [](std::string_view) {});

    LT_CHECK(fired);
LT_END_AUTO_TEST(fire_connection_opened_invokes_registered_hook)

// A throwing hook must be contained (fire_* is noexcept) and reported
// through the on_error callback rather than propagating.
LT_BEGIN_AUTO_TEST(hook_bus_suite, fire_routes_hook_exception_through_on_error)
    htd::hook_bus bus;
    bus.add(ht::hook_phase::connection_opened,
        std::function<void(const ht::connection_open_ctx&)>(
            [](const ht::connection_open_ctx&) {
                throw std::runtime_error("boom");
            }));

    std::string captured;
    ht::connection_open_ctx ctx{};
    bus.fire_connection_opened(
        ctx, [&captured](std::string_view msg) { captured = std::string(msg); });

    LT_CHECK(captured.find("boom") != std::string::npos);
LT_END_AUTO_TEST(fire_routes_hook_exception_through_on_error)

// ----- Firing: short-circuit-capable phases --------------------------------

LT_BEGIN_AUTO_TEST(hook_bus_suite,
                   fire_before_handler_short_circuits_on_respond_with)
    htd::hook_bus bus;
    bus.add(ht::hook_phase::before_handler,
        std::function<ht::hook_action(ht::before_handler_ctx&)>(
            [](ht::before_handler_ctx&) {
                return ht::hook_action::respond_with(
                    ht::http_response::string("short-circuit"));
            }));

    ht::before_handler_ctx ctx{};
    auto resp = bus.fire_before_handler(ctx, [](std::string_view) {});

    LT_CHECK(resp.has_value());
LT_END_AUTO_TEST(fire_before_handler_short_circuits_on_respond_with)

LT_BEGIN_AUTO_TEST(hook_bus_suite,
                   fire_before_handler_returns_nullopt_when_all_hooks_pass)
    htd::hook_bus bus;
    bus.add(ht::hook_phase::before_handler,
        std::function<ht::hook_action(ht::before_handler_ctx&)>(
            [](ht::before_handler_ctx&) { return ht::hook_action::pass(); }));

    ht::before_handler_ctx ctx{};
    auto resp = bus.fire_before_handler(ctx, [](std::string_view) {});

    LT_CHECK(!resp.has_value());
LT_END_AUTO_TEST(fire_before_handler_returns_nullopt_when_all_hooks_pass)

// ----- Alias slots -----------------------------------------------------
//
// handler_exception_alias_ is documented to ALSO arm the any_hooks_ gate
// for its phase, even with zero user-vector hooks -- unlike
// log_access_alias_, which does not (fire_response_sent_gated checks
// has_log_access_alias() explicitly instead). Both invariants are pinned
// below.

LT_BEGIN_AUTO_TEST(hook_bus_suite,
                   handler_exception_alias_arms_gate_without_user_hooks)
    htd::hook_bus bus;
    LT_CHECK(!bus.has_handler_exception_alias());
    LT_CHECK(!bus.has_hooks_for(ht::hook_phase::handler_exception));

    bus.set_handler_exception_alias(
        [](const ht::handler_exception_ctx&) { return ht::hook_action::pass(); });

    LT_CHECK(bus.has_handler_exception_alias());
    LT_CHECK(bus.has_hooks_for(ht::hook_phase::handler_exception));
    // The alias is a fallback slot, not a user-vector entry.
    LT_CHECK_EQ(bus.phase_hook_count(ht::hook_phase::handler_exception),
                static_cast<std::size_t>(0));
LT_END_AUTO_TEST(handler_exception_alias_arms_gate_without_user_hooks)

// fire_handler_exception's documented tail: after the (empty) user
// vector, the alias slot fires and its short-circuit response is
// returned.
LT_BEGIN_AUTO_TEST(hook_bus_suite, fire_handler_exception_falls_through_to_alias)
    htd::hook_bus bus;
    bool alias_called = false;
    bus.set_handler_exception_alias(
        [&alias_called](const ht::handler_exception_ctx&) {
            alias_called = true;
            return ht::hook_action::respond_with(
                ht::http_response::string("fallback"));
        });

    ht::handler_exception_ctx ctx{};
    auto resp = bus.fire_handler_exception(ctx, [](std::string_view) {});

    LT_CHECK(alias_called);
    LT_CHECK(resp.has_value());
LT_END_AUTO_TEST(fire_handler_exception_falls_through_to_alias)

LT_BEGIN_AUTO_TEST(hook_bus_suite, log_access_alias_does_not_arm_any_hooks_gate)
    htd::hook_bus bus;
    LT_CHECK(!bus.has_log_access_alias());
    LT_CHECK(!bus.has_hooks_for(ht::hook_phase::response_sent));

    bus.set_log_access_alias([](const ht::response_sent_ctx&) {});

    LT_CHECK(bus.has_log_access_alias());
    // Documented divergence from handler_exception_alias_: setting the
    // log_access alias does NOT arm any_hooks_[response_sent].
    LT_CHECK(!bus.has_hooks_for(ht::hook_phase::response_sent));
LT_END_AUTO_TEST(log_access_alias_does_not_arm_any_hooks_gate)

LT_BEGIN_AUTO_TEST(hook_bus_suite, fire_response_sent_invokes_log_access_alias)
    htd::hook_bus bus;
    bool alias_called = false;
    bus.set_log_access_alias(
        [&alias_called](const ht::response_sent_ctx&) { alias_called = true; });

    ht::response_sent_ctx ctx{};
    bus.fire_response_sent(ctx, [](std::string_view) {});

    LT_CHECK(alias_called);
LT_END_AUTO_TEST(fire_response_sent_invokes_log_access_alias)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
