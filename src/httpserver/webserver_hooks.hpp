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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

// webserver_hooks.hpp — lifecycle-hook registration surface of class
// webserver (TASK-045 / §4.10 / DR-012). Carries member-function
// DECLARATIONS only; meant to be included from WITHIN the body of
// `class webserver` in httpserver/webserver.hpp.
#ifndef SRC_HTTPSERVER_WEBSERVER_HOOKS_HPP_
#define SRC_HTTPSERVER_WEBSERVER_HOOKS_HPP_

#ifndef SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_
#error "httpserver/webserver_hooks.hpp must be included from inside the webserver class body in <httpserver/webserver.hpp>."
#endif

/**
 * @brief Register a hook on a server-wide lifecycle phase
 * (TASK-045 / §4.10 / DR-012).
 *
 * Eleven overloads, one per @ref httpserver::hook_phase value. The
 * accepted phases are:
 *
 *   - `connection_opened`   (observation-only)
 *   - `accept_decision`     (observation-only)
 *   - `connection_closed`   (observation-only)
 *   - `request_received`    (short-circuit-capable; returns hook_action)
 *   - `body_chunk`          (short-circuit-capable; returns hook_action)
 *   - `route_resolved`      (observation-only)
 *   - `before_handler`      (short-circuit-capable; returns hook_action)
 *   - `handler_exception`   (short-circuit-capable; returns hook_action)
 *   - `after_handler`       (short-circuit-capable; returns hook_action)
 *   - `response_sent`       (observation-only)
 *   - `request_completed`   (observation-only)
 *
 * The overload set is distinguished by the @c std::function template
 * argument; the @ref httpserver::hook_phase parameter is validated at
 * runtime against the overload's phase and a mismatch throws
 * @c std::invalid_argument. In practice the compiler picks the right
 * overload from the std::function's signature, so the runtime guard
 * catches only the unusual case of a hand-built std::function being
 * passed against the wrong phase tag.
 *
 * Returns a move-only @ref httpserver::hook_handle whose destructor
 * (or explicit @ref httpserver::hook_handle::remove ) erases the
 * registration. Call @ref httpserver::hook_handle::detach to disarm
 * the destructor so the registration persists for the webserver's
 * lifetime.
 *
 * Per-route variants of the five post-route-resolution phases are
 * available via @ref httpserver::http_resource::add_hook .
 *
 * @param phase   the lifecycle phase to register on. Must match the
 *                phase associated with the std::function signature.
 * @param fn      the callable to invoke when the phase fires.
 * @return        a @ref httpserver::hook_handle that erases the registration on
 *                destruction (unless detach()-ed).
 * @throws std::invalid_argument if @p phase does not match the
 *                overload's compile-time phase, or if @p fn is
 *                empty.
 */
hook_handle add_hook(hook_phase phase,
    std::function<void(const connection_open_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<void(const accept_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<hook_action(request_received_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<hook_action(body_chunk_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<void(const route_resolved_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<hook_action(before_handler_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<hook_action(const handler_exception_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<hook_action(after_handler_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<void(const response_sent_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<void(const request_completed_ctx&)> fn);
/// @copydoc add_hook(hook_phase, std::function<void(const connection_open_ctx&)>)
hook_handle add_hook(hook_phase phase,
    std::function<void(const connection_close_ctx&)> fn);

#if defined(HTTPSERVER_COMPILATION)
// TASK-045: tiny static factory for an armed hook_handle. Bridges
// the anonymous-namespace register_hook_impl helper in
// src/webserver.cpp into hook_handle's private constructor without
// widening hook_handle's friend list. Visible only when compiling
// libhttpserver itself (gated on HTTPSERVER_COMPILATION) -- the
// detail::webserver_impl* argument is internal regardless, so
// installed consumers cannot reach this entry point.
static hook_handle make_hook_handle_(detail::webserver_impl* impl,
                                     hook_phase phase,
                                     std::uint64_t slot_id) noexcept;

// TASK-048: install the three default hook-bus aliases at webserver
// construction. For each of `not_found_handler`, `method_not_allowed_handler`,
// and `auth_handler` that the user set on the builder, registers a hook
// at the matching phase (route_resolved, before_handler, before_handler).
// The hooks are observation-only stubs whose presence is the alias
// relationship; the existing inline dispatch code continues to consult
// the user-supplied callable, so the on-the-wire behaviour is byte-for-
// byte identical to v1.
//
// Called once from the webserver ctor body; never re-called. The
// registrations are detach()-ed so they live for the webserver's
// lifetime.
void install_default_alias_hooks_();
// Per-alias install helpers carved out of install_default_alias_hooks_
// so the parent stays inside the project-wide CCN gate.
void install_auth_alias_();
void install_method_not_allowed_alias_();
void install_not_found_alias_();
#endif

#endif  // SRC_HTTPSERVER_WEBSERVER_HOOKS_HPP_
