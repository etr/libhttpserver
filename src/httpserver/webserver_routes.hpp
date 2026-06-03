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

// webserver_routes.hpp — registration / routing surface of class webserver.
//
// Carries member-function DECLARATIONS only; meant to be included from
// WITHIN the body of `class webserver` defined in
// httpserver/webserver.hpp. Including it elsewhere raises a #error.
// Covers register_path / register_prefix / register_resource (templated
// + shared_ptr overloads), the on_* HTTP-verb shortcuts (on_get,
// on_post, on_put, on_delete, on_patch, on_options, on_head), the
// table-driven route() entry points, and the matching unregister_*
// counterparts.
#ifndef SRC_HTTPSERVER_WEBSERVER_ROUTES_HPP_
#define SRC_HTTPSERVER_WEBSERVER_ROUTES_HPP_

#ifndef SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_
#error "httpserver/webserver_routes.hpp must be included from inside the webserver class body in <httpserver/webserver.hpp>."
#endif

/**
 * Register a resource for an exact (non-prefix) URL match.
 *
 * The path matches only itself, including any parameterized form:
 * `register_path("/users/{id}")` matches `/users/42` but NOT
 * `/users/42/profile`.
 *
 * Accepts a derived resource type; ownership is transferred to the
 * webserver. Calls with derived types resolve unambiguously.
 *
 * Throws std::invalid_argument if the resource pointer is null,
 * if the path conflicts with single_resource mode (single_resource
 * requires register_prefix), or if a resource is already
 * registered at the same path.
 *
 * @param path The url pointing to the resource. May be parameterized
 *             in the form /path/to/url/{par1}/and/{par2} or a regex.
 * @param res  unique_ptr to the http_resource (or any derived type);
 *             ownership is transferred to the webserver.
 * @see register_prefix, unregister_path
**/
template <typename T,
          typename = std::enable_if_t<
              std::is_base_of_v<http_resource, T>>>
void register_path(const std::string& path, std::unique_ptr<T> res) {
    register_path(path,
                  std::shared_ptr<http_resource>(std::move(res)));
}
/**
 * Register a resource for an exact-match URL (shared_ptr overload).
 *
 * Identical contract to @ref register_path(const std::string&, std::unique_ptr<T>)
 * but lets the caller retain a reference to the resource; the
 * webserver keeps one reference until @ref unregister_path or
 * destruction releases it.
 *
 * @param path The url pointing to the resource. May be parameterized
 *             in the form /path/to/url/{par1}/and/{par2} or a regex.
 * @param res  shared_ptr to the http_resource; the caller retains
 *             a reference.
**/
void register_path(const std::string& path,
                   std::shared_ptr<http_resource> res);

/**
 * Register a resource for a prefix URL match (the path and all its
 * children match).
 *
 * `register_prefix("/static")` matches `/static`, `/static/x`,
 * `/static/anything/here`, etc.
 *
 * Accepts a derived resource type; ownership is transferred to the
 * webserver.
 *
 * **Catch-all semantics:** `register_prefix("")` or
 * `register_prefix("/")` registers a catch-all handler that matches
 * every URL not covered by a more specific registration. This is
 * intentional but can be surprising — ensure it is what you want
 * before registering an empty or bare root prefix outside of
 * single_resource mode.
 *
 * Throws std::invalid_argument if the resource pointer is null,
 * or if a resource is already registered at the same path.
 *
 * @param path The url whose subtree this resource handles.
 * @param res  unique_ptr to the http_resource (or any derived type).
 * @see register_path, unregister_prefix
**/
template <typename T,
          typename = std::enable_if_t<
              std::is_base_of_v<http_resource, T>>>
void register_prefix(const std::string& path, std::unique_ptr<T> res) {
    register_prefix(path,
                    std::shared_ptr<http_resource>(std::move(res)));
}
/**
 * Register a resource for a prefix URL match (shared_ptr overload).
 *
 * Identical contract to @ref register_prefix(const std::string&, std::unique_ptr<T>)
 * but lets the caller retain a reference to the resource.
 *
 * @param path The url whose subtree this resource handles.
 * @param res  shared_ptr to the http_resource; the caller retains
 *             a reference.
**/
void register_prefix(const std::string& path,
                     std::shared_ptr<http_resource> res);

/**
 * Deprecated alias for register_path(). Kept for backward compatibility;
 * use register_path() for exact match or register_prefix() for prefix match.
 *
 * @param path The url pointing to the resource.
 * @param res  unique_ptr to the http_resource (or any derived type).
**/
template <typename T,
          typename = std::enable_if_t<
              std::is_base_of_v<http_resource, T>>>
[[deprecated("use register_path() for exact match or register_prefix() for prefix match")]]
// This file is included inside the webserver class body; transitive
// <utility>/<memory>/<string> live in the parent webserver.hpp.
void register_resource(const std::string& path, std::unique_ptr<T> res) {  // NOLINT(build/include_what_you_use)
    register_path(path, std::move(res));  // NOLINT(build/include_what_you_use)
}
/// @copydoc register_resource(const std::string&, std::unique_ptr<T>)
[[deprecated("use register_path() for exact match or register_prefix() for prefix match")]]
void register_resource(const std::string& path,
                       std::shared_ptr<http_resource> res);  // NOLINT(build/include_what_you_use)

/**
 * Register a lambda handler for HTTP GET on @p path.
 *
 * The seven on_* entry points (on_get, on_post, on_put, on_delete,
 * on_patch, on_options, on_head) let stateless endpoints be
 * registered without subclassing http_resource. Each accepts a
 * std::function<http_response(const http_request&)>.
 *
 * Multiple on_* calls on the SAME path COMPOSE: each call adds the
 * matching method bit to a single route entry. A second on_get on
 * the same path -- or on_get after another on_* already covers GET
 * on this path -- throws std::invalid_argument. Mixing class-based
 * registration (register_path / register_prefix) and lambda
 * registration on the same path also throws.
 *
 * @param path    URL path; may be parameterized as /foo/{id}.
 * @param handler invoked per request; returns http_response by value.
**/
void on_get(const std::string& path,
            std::function<http_response(const http_request&)> handler);
/// @copydoc on_get(const std::string&, std::function<http_response(const http_request&)>)
void on_post(const std::string& path,
             std::function<http_response(const http_request&)> handler);
/// @copydoc on_get(const std::string&, std::function<http_response(const http_request&)>)
void on_put(const std::string& path,
            std::function<http_response(const http_request&)> handler);
/// @copydoc on_get(const std::string&, std::function<http_response(const http_request&)>)
void on_delete(const std::string& path,
               std::function<http_response(const http_request&)> handler);
/// @copydoc on_get(const std::string&, std::function<http_response(const http_request&)>)
void on_patch(const std::string& path,
              std::function<http_response(const http_request&)> handler);
/// @copydoc on_get(const std::string&, std::function<http_response(const http_request&)>)
void on_options(const std::string& path,
                std::function<http_response(const http_request&)> handler);
/// @copydoc on_get(const std::string&, std::function<http_response(const http_request&)>)
void on_head(const std::string& path,
             std::function<http_response(const http_request&)> handler);

/**
 * Generic table-driven lambda registration.
 *
 * `on_get`, `on_post`, ... are the preferred call-site form when
 * the HTTP method is known statically; `route()` is the escape
 * hatch for cases where the method is a runtime value
 * (config-driven route tables, programmatic registration loops).
 *
 * Both forms share the same internal registration path: a single
 * `route(http_method, path, h)` is exactly equivalent to the
 * matching `on_*(path, h)`, and `route(method_set, path, h)` is
 * exactly equivalent to one `on_*` call per set bit, applied
 * atomically -- if any one of the bits would conflict with an
 * existing registration, no slot is mutated and the call throws.
 *
 * Throws std::invalid_argument if @p handler is empty, if @p m
 * is `http_method::count_` (sentinel), if the path conflicts
 * with single_resource mode, if a class-based resource is
 * already registered at the path, or if a lambda is already
 * registered for any of the requested methods on this path.
 *
 * @param m       HTTP method to register the handler under.
 * @param path    URL path; may be parameterized as /foo/{id}.
 * @param handler invoked per request; returns http_response by value.
**/
void route(http_method m,
           const std::string& path,
           std::function<http_response(const http_request&)> handler);

/**
 * Multi-method form of route(): register the same handler for
 * every method bit set in @p methods, atomically (all-or-nothing).
 *
 * Useful when one handler should serve more than one method
 * (e.g., GET and HEAD on the same path). Equivalent to one
 * `on_*` call per set bit, but if any one of those would conflict
 * with an existing registration, no slot is mutated and the call
 * throws std::invalid_argument.
 *
 * Throws std::invalid_argument if @p methods is empty, if
 * @p handler is empty, if the path conflicts with single_resource
 * mode, if a class-based resource is already registered at the
 * path, or if a lambda is already registered for any of the
 * requested methods on this path.
 *
 * @param methods bitmask of methods to register the handler for.
 * @param path    URL path; may be parameterized as /foo/{id}.
 * @param handler invoked per request; returns http_response by value.
**/
void route(method_set methods,
           const std::string& path,
           std::function<http_response(const http_request&)> handler);

/**
 * Unregister an exact-match (register_path) registration.
 * No-op if no exact registration exists at @p path.
 *
 * @param path the exact URL previously passed to @ref register_path.
 * @see register_path, unregister_prefix, unregister_resource
**/
void unregister_path(const std::string& path);

/**
 * Unregister a prefix-match (register_prefix) registration.
 * No-op if no prefix registration exists at @p path.
 *
 * @param path the prefix URL previously passed to @ref register_prefix.
 * @see register_prefix, unregister_path, unregister_resource
**/
void unregister_prefix(const std::string& path);

/**
 * Kind-agnostic convenience: erases either an exact or a prefix
 * registration at @p path. Equivalent to calling unregister_path
 * and unregister_prefix; idempotent.
 *
 * @param path the URL previously passed to @ref register_path or @ref register_prefix.
 * @see register_path, register_prefix, unregister_path, unregister_prefix
**/
// NOLINTNEXTLINE(build/include_what_you_use) -- see class-body include note above.
void unregister_resource(const std::string& path);

#endif  // SRC_HTTPSERVER_WEBSERVER_ROUTES_HPP_
