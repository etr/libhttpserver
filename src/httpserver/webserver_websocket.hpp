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

// webserver_websocket.hpp — websocket registration surface of class
// webserver. Carries member-function DECLARATIONS only; meant to be
// included from WITHIN the body of `class webserver` in
// httpserver/webserver.hpp.
#ifndef SRC_HTTPSERVER_WEBSERVER_WEBSOCKET_HPP_
#define SRC_HTTPSERVER_WEBSERVER_WEBSOCKET_HPP_

#ifndef SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_
#error "httpserver/webserver_websocket.hpp must be included from inside the webserver class body in <httpserver/webserver.hpp>."
#endif

/**
 * Register a websocket handler for @p resource (PRD-HDL-REQ-003 /
 * PRD-HDL-REQ-005 / DR-010; PRD-FLG-REQ-001 / §7).
 *
 * Declared unconditionally so the public surface is identical in
 * HAVE_WEBSOCKET-on and HAVE_WEBSOCKET-off builds. When the library
 * was built without HAVE_WEBSOCKET this call throws
 * feature_unavailable("websocket", "HAVE_WEBSOCKET").
 *
 * TASK-035: this is the smart-pointer-ownership replacement for the
 * v1 raw-pointer overload. The templated unique_ptr<T> shim mirrors
 * TASK-023's pattern so callers can pass `std::make_unique<my_ws>()`
 * without an explicit base cast; it funnels into the shared_ptr
 * overload below.
 *
 * Throws std::invalid_argument if the handler is null or if a
 * handler is already registered at the same path (mirrors the rest
 * of the v2.0 registration surface).
 *
 * @pre Must be called before @ref start(). MHD_ALLOW_UPGRADE is set
 *      at daemon-start time based on whether any WebSocket handlers are
 *      registered; handlers registered after start() will not be reachable
 *      because the daemon will not have been started with the upgrade flag.
 *
 * @param resource The url at which to register the handler.
 * @param handler  unique_ptr to the websocket_handler (or any
 *                 derived type); ownership is transferred to the
 *                 webserver.
 * @see unregister_ws_resource, feature_unavailable, features
 **/
template <typename T,
          typename = std::enable_if_t<
              std::is_base_of_v<websocket_handler, T>>>
// This file is included inside the webserver class body; transitive
// <utility>/<memory>/<string> live in the parent webserver.hpp.
void register_ws_resource(const std::string& resource,  // NOLINT(build/include_what_you_use)
                          std::unique_ptr<T> handler) {
    register_ws_resource(
        resource,
        std::shared_ptr<websocket_handler>(std::move(handler)));  // NOLINT(build/include_what_you_use)
}
/**
 * Register a websocket handler at @p resource (shared_ptr overload).
 *
 * Identical contract to @ref register_ws_resource(const std::string&, std::unique_ptr<T>)
 * but lets the caller retain a reference to the handler.
 *
 * @pre Must be called before @ref start(). See the unique_ptr overload above.
 *
 * @param resource The url at which to register the handler.
 * @param handler  shared_ptr to the websocket_handler; the caller
 *                 retains a reference.
 **/
void register_ws_resource(const std::string& resource,
                          std::shared_ptr<websocket_handler> handler);  // NOLINT(build/include_what_you_use)

/**
 * Drop the websocket handler registered at @p resource (PRD-HDL-REQ-003).
 *
 * No-op if no handler is registered at the path (mirrors the
 * semantics of unregister_path). The handler's destructor runs
 * when the last shared_ptr reference goes away -- the webserver
 * always holds one reference until this call (or destruction)
 * drops it.
 *
 * Throws feature_unavailable on a HAVE_WEBSOCKET-off build.
 *
 * @param resource the URL previously passed to @ref register_ws_resource.
 * @see register_ws_resource, feature_unavailable, features
 **/
// NOLINTNEXTLINE(build/include_what_you_use) -- see class-body include note above.
void unregister_ws_resource(const std::string& resource);

#endif  // SRC_HTTPSERVER_WEBSERVER_WEBSOCKET_HPP_
