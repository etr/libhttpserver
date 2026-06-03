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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_HOOK_PHASE_HPP_
#define SRC_HTTPSERVER_HOOK_PHASE_HPP_

#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @file hook_phase.hpp
 * @brief Enumerates the eleven lifecycle phases at which user hooks fire.
 *
 * TASK-045 / DR-012 / §4.10 / PRD-HOOK-REQ-001..002. Storage and firing
 * semantics live in detail/webserver_impl.hpp; per-phase callable
 * signatures live in hook_context.hpp and the matching
 * webserver::add_hook / http_resource::add_hook overloads.
 */
namespace httpserver {

/**
 * @brief Enumerates the eleven points along the request / response
 * lifecycle at which user-registered hooks may fire.
 *
 * Phases are listed in firing order. `count_` is a sentinel and must
 * remain last; it is not a valid phase value. The underlying type is
 * `std::uint8_t` -- two orders of magnitude of growth headroom past
 * the current eleven phases.
 *
 * Five phases (`before_handler`, `handler_exception`, `after_handler`,
 * `response_sent`, `request_completed`) are also valid arguments to
 * `http_resource::add_hook` for per-route registration. The other six
 * are server-wide only.
 *
 * See `specs/architecture/04-components/hooks.md` §4.10 and DR-012.
 */
enum class hook_phase : std::uint8_t {
    connection_opened,
    accept_decision,
    request_received,
    body_chunk,
    route_resolved,
    before_handler,
    handler_exception,
    after_handler,
    response_sent,
    request_completed,
    connection_closed,
    count_    // sentinel; must remain last
};

static_assert(static_cast<std::size_t>(hook_phase::count_) == 11u,
              "hook_phase::count_ must be 11");

/**
 * @brief Spec-canonical name of a phase, for error messages and logs.
 *
 * Total over the eleven declared enumerators; any other underlying
 * value (only producible via `static_cast`) returns an empty view
 * rather than crashing. Used by the wrong-phase `add_hook` throw site
 * and by hook telemetry / docs.
 *
 * @param p phase to name.
 * @return canonical name as a `std::string_view`, or an empty view if
 *         `p` is `count_` or an out-of-range underlying value.
 */
constexpr std::string_view to_string(hook_phase p) noexcept {
    // Names indexed by the hook_phase underlying value. Keep this array
    // in lockstep with the enum declaration above. A constexpr lookup
    // beats a 12-case switch on the CCN gate and codegens to the same
    // jump table on a modern optimiser.
    constexpr std::string_view kNames[] = {
        "connection_opened",
        "accept_decision",
        "request_received",
        "body_chunk",
        "route_resolved",
        "before_handler",
        "handler_exception",
        "after_handler",
        "response_sent",
        "request_completed",
        "connection_closed",
    };
    const auto idx = static_cast<std::size_t>(p);
    if (idx >= sizeof(kNames) / sizeof(kNames[0])) return {};
    return kNames[idx];
}

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_PHASE_HPP_
