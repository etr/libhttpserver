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

// create_webserver_setters.hpp — handler/callback/TLS-tuning setter
// surface of the create_webserver fluent builder.
//
// This header carries inline member DEFINITIONS only; it is meant to be
// included from WITHIN the body of `class create_webserver` defined in
// httpserver/create_webserver.hpp. Including it in any other context
// produces a compile error (see the inner gate below). It groups the
// doc-comment-heavy handler/callback setters (not_found_handler,
// method_not_allowed_handler, internal_error_handler,
// file_cleanup_callback, auth_handler, auth_skip_paths, sni_callback)
// and the remaining TLS / connection-tuning setters.
//
// Keeping this surface in its own file lets the create_webserver.hpp
// class definition stay under the project-wide per-file line-count
// ceiling (FILE_LOC_MAX in scripts/check-file-size.sh) without splitting
// the public class across translation units. Every type and helper the
// definitions below reference (the callback typedefs, the private member
// fields, and the static throw_invalid / check_non_negative helpers)
// lives in create_webserver.hpp and is in scope here because this file
// is expanded inside the class body.
#ifndef SRC_HTTPSERVER_CREATE_WEBSERVER_SETTERS_HPP_
#define SRC_HTTPSERVER_CREATE_WEBSERVER_SETTERS_HPP_

#ifndef SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_INSIDE_CLASS_
#error "httpserver/create_webserver_setters.hpp must be included from inside the create_webserver class body in <httpserver/create_webserver.hpp>."
#endif

     /**
      * Install a handler invoked when no resource matches the request path (HTTP 404).
      *
      * The handler returns an @ref httpserver::http_response by value; its status
      * code, headers, and body are sent on the wire as-is. If null, a
      * default 404 response is generated.
      *
      * @note This is an alias. Calling it (with a non-null callable)
      *       installs a hook at @ref httpserver::hook_phase::route_resolved.
      *       Equivalent to `ws.add_hook(hook_phase::route_resolved, ...)`
      *       at webserver construction (TASK-048 / PRD-HOOK-REQ-009 / §4.10).
      *       The on-the-wire 404 synthesis flows through the v1 dispatch
      *       path; the hook registration is the alias-equivalence story.
      *
      * @param h error_handler callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      * @see method_not_allowed_handler, internal_error_handler
      */
     create_webserver& not_found_handler(error_handler h) { _not_found_handler = std::move(h); return *this; }
     /**
      * Install a handler invoked when a resource matches the path but
      * not the HTTP method (HTTP 405).
      *
      * The handler returns an @ref httpserver::http_response by value. If null, a
      * default 405 response is generated.
      *
      * @note This is an alias. Calling it (with a non-null callable)
      *       installs a hook at @ref httpserver::hook_phase::before_handler.
      *       Equivalent to `ws.add_hook(hook_phase::before_handler, ...)`
      *       at webserver construction (TASK-048 / PRD-HOOK-REQ-009 / §4.10).
      *       The on-the-wire 405 synthesis flows through the v1 dispatch
      *       path; the hook registration is the alias-equivalence story.
      *
      * @param h error_handler callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      * @see not_found_handler, internal_error_handler
      */
     create_webserver& method_not_allowed_handler(error_handler h) { _method_not_allowed_handler = std::move(h); return *this; }
     /**
      * Install the handler invoked when a registered request handler
      * throws (HTTP 500 by default).
      *
      * The @p h callback receives the exception message (from `e.what()`
      * for `std::exception`, or `"unknown exception"` otherwise) and the
      * originating @ref httpserver::http_request, and returns the response to send.
      * If null, a default 500 with the fixed body
      * `"Internal Server Error"` is sent (DR-009 Revision 1 / TASK-055,
      * CWE-209 fix); the originating message is still surfaced via the
      * configured `log_error` callback. The v1 behaviour of including
      * the message in the default body is opt-in via
      * @ref httpserver::create_webserver::expose_exception_messages — development
      * only.
      * The callback must be thread-safe (may be invoked concurrently
      * from multiple MHD worker threads).
      *
      * For the full six-point dispatch exception contract see the
      * @ref httpserver::webserver class-level block (DR-009 §5.2 / PRD-FLG-REQ-002).
      *
      * @note This is an alias. Calling it with a non-null callable
      *       installs the callable as a LAST-position hook at
      *       @ref httpserver::hook_phase::handler_exception. Equivalent
      *       to `ws.add_hook(hook_phase::handler_exception, ...)`
      *       at webserver construction, except that the alias slot
      *       ALWAYS fires last in the chain -- user-added
      *       handler_exception hooks added via `add_hook` run first and
      *       may short-circuit before the alias is reached. See DR-012
      *       / §4.10 / PRD-HOOK-REQ-009.
      *
      *       Throwing-hook semantics (per DR-012): a throwing
      *       handler_exception hook -- user-added or this alias -- is
      *       caught and the chain CONTINUES to the next hook. If every
      *       hook (including this alias) either throws or returns
      *       @ref httpserver::hook_action::pass(), the dispatcher falls
      *       back to the hardcoded empty-body 500 (DR-009 §5.2 point 4)
      *       WITHOUT re-invoking @p h.
      *
      * @param h @ref httpserver::internal_error_handler_t callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      * @see webserver (DR-009 §5.2 contract), not_found_handler, method_not_allowed_handler
      */
     create_webserver& internal_error_handler(internal_error_handler_t h) { _internal_error_handler = std::move(h); return *this; }
     create_webserver& file_cleanup_callback(file_cleanup_callback_ptr v) { _file_cleanup_callback = std::move(v); return *this; }
     /**
      * Install the centralised auth handler invoked before every
      * dispatched request whose path is not on the @ref auth_skip_paths
      * list. Returning an engaged `std::optional<http_response>` short-
      * circuits dispatch and sends that response on the wire (typically a
      * 401 or 403). Returning `std::nullopt` allows the request to proceed.
      *
      * @note This is an alias. Calling it (with a non-null callable)
      *       installs a hook at @ref httpserver::hook_phase::before_handler.
      *       Equivalent to `ws.add_hook(hook_phase::before_handler, ...)`
      *       at webserver construction (TASK-048 / PRD-HOOK-REQ-009 / §4.10).
      *       The on-the-wire auth short-circuit flows through the v1
      *       dispatch path; the hook registration is the
      *       alias-equivalence story.
      *
      * @note **Auth is not applied to unmatched (404) paths.** The handler
      *       is only invoked when a registered resource was found for the
      *       request URL. Requests to URLs with no registered resource fall
      *       through to the 404 page without auth, allowing unauthenticated
      *       clients to distinguish 404 from 401 and enumerate which routes
      *       exist. If uniform 401 behaviour for all paths (including
      *       unregistered ones) is required to prevent route enumeration,
      *       use a wildcard resource registered at "/" or implement the
      *       check inside the `not_found_handler`.
      *
      * @param v @ref httpserver::auth_handler_ptr callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      */
     create_webserver& auth_handler(auth_handler_ptr v) { _auth_handler = std::move(v); return *this; }

     create_webserver& auth_skip_paths(std::vector<std::string> v) { _auth_skip_paths = std::move(v); return *this; }
     create_webserver& sni_callback(sni_callback_t v) { _sni_callback = std::move(v); return *this; }

     // TASK-033: renamed from no_listen_socket()/no_thread_safety()/no_alpn();
     // public-API polarity is inverted (private field still stores the "no_"
     // form to avoid churning webserver.cpp).
     create_webserver& listen_socket(bool enable = true) { _no_listen_socket = !enable; return *this; }
     create_webserver& thread_safety(bool enable = true) { _no_thread_safety = !enable; return *this; }
     create_webserver& alpn(bool enable = true) { _no_alpn = !enable; return *this; }

     create_webserver& turbo(bool enable = true) { _turbo = enable; return *this; }
     create_webserver& suppress_date_header(bool enable = true) { _suppress_date_header = enable; return *this; }
     create_webserver& listen_backlog(int v) { check_non_negative("listen_backlog", v); _listen_backlog = v; return *this; }
     create_webserver& address_reuse(int v) { check_non_negative("address_reuse", v); _address_reuse = v; return *this; }
     create_webserver& connection_memory_increment(size_t v) { _connection_memory_increment = v; return *this; }
     create_webserver& tcp_fastopen_queue_size(int v) { check_non_negative("tcp_fastopen_queue_size", v); _tcp_fastopen_queue_size = v; return *this; }
     create_webserver& sigpipe_handled_by_app(bool enable = true) { _sigpipe_handled_by_app = enable; return *this; }
     create_webserver& https_mem_dhparams(std::string v) { _https_mem_dhparams = std::move(v); return *this; }
     /**
      * Set the passphrase for the TLS private key (PEM decryption).
      *
      * @warning The passphrase is stored as a plain `std::string` for the
      *          lifetime of this builder and the @ref httpserver::webserver constructed
      *          from it. Callers who need to minimise key-material exposure
      *          should overwrite or destroy the builder after
      *          `webserver ws{create_webserver{...}.https_key_password(pw)}`.
      *
      * @param v the PEM passphrase string.
      * @return reference to this builder for chaining.
      */
     create_webserver& https_key_password(std::string v) { _https_key_password = std::move(v); return *this; }
     create_webserver& https_priorities_append(std::string v) { _https_priorities_append = std::move(v); return *this; }  // NOLINT(build/include_what_you_use)
     create_webserver& client_discipline_level(int v) {
         if (v < -1) throw_invalid("client_discipline_level", v, ">= -1");
         _client_discipline_level = v; return *this;
     }

#endif  // SRC_HTTPSERVER_CREATE_WEBSERVER_SETTERS_HPP_
