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

#ifndef SRC_HTTPSERVER_FEATURE_UNAVAILABLE_HPP_
#define SRC_HTTPSERVER_FEATURE_UNAVAILABLE_HPP_

#include <stdexcept>
#include <string>
#include <string_view>

namespace httpserver {

/**
 * Exception thrown when a build-time-disabled feature is invoked at runtime.
 *
 * The class is unconditionally available regardless of `HAVE_*` flags so
 * that downstream code can always write
 * @code
 *     try { ... } catch (const httpserver::feature_unavailable&) { ... }
 * @endcode
 * even in builds that compiled out the optional feature in question.
 *
 * The class is header-only (and inline) on purpose: it has no library
 * dependencies, must be cheap to throw from anywhere in the codebase, and
 * avoids ABI churn for what is effectively a labelled `std::runtime_error`.
 *
 * ### Throw sites (architecture spec §7)
 *
 * The library throws `feature_unavailable` from these sites; each one
 * pairs the feature label with the `HAVE_*` build flag that gates it:
 *
 *   - @ref webserver::webserver — thrown at webserver construction time
 *     when consuming the builder: `use_ssl(true)` on a
 *     `HAVE_GNUTLS`-off build, `basic_auth(true)` on a
 *     `HAVE_BAUTH`-off build, or `digest_auth(true)` on a
 *     `HAVE_DAUTH`-off build. The `create_webserver` setters accept all
 *     values without throwing; feature-unavailability is validated
 *     lazily at `webserver` construction, not at the setter call.
 *   - @ref webserver::register_ws_resource and
 *     @ref webserver::unregister_ws_resource — on a `HAVE_WEBSOCKET`-off
 *     build (every websocket entry point throws this).
 *   - @ref websocket_session::send_text, @ref websocket_session::send_binary,
 *     @ref websocket_session::send_ping, @ref websocket_session::send_pong,
 *     and @ref websocket_session::close — on a `HAVE_WEBSOCKET`-off build,
 *     so that downstream handlers that capture a session reference get a
 *     uniform exception type rather than a link-time error.
 *
 * Catching `feature_unavailable` (or its base `std::runtime_error`) on
 * the dispatch path is handled by the standard
 * @ref webserver internal_error_handler contract — it surfaces as a
 * generic 500 unless the application installs an
 * @ref create_webserver::internal_error_handler that special-cases it.
 *
 * @see create_webserver::use_ssl, create_webserver::basic_auth,
 *      create_webserver::digest_auth, webserver::register_ws_resource,
 *      websocket_session
 */
class feature_unavailable : public std::runtime_error {
 public:
    /**
     * Construct with the feature name and the build flag that gates it.
     *
     * The resulting `what()` is
     * `"feature '<feature>' unavailable: built without <build_flag>"`.
     *
     * @param feature    human-readable feature label (e.g. `"TLS"`, `"WebSocket"`).
     *                   Both views must remain valid for the duration of this
     *                   constructor call; they are consumed before the constructor
     *                   returns and need not outlive it.
     * @param build_flag the autoconf-defined `HAVE_*` flag that was off
     *                   in this build (e.g. `"HAVE_GNUTLS"`).
     *                   See @p feature for the lifetime requirement.
     */
    feature_unavailable(std::string_view feature, std::string_view build_flag)
        : std::runtime_error([&] {
            // Fixed-text portion: "feature '' unavailable: built without "
            // Computed at compile time to stay in sync with any future wording change.
            static constexpr std::size_t k_fixed_overhead =
                std::string_view("feature '' unavailable: built without ").size();
            std::string msg;
            msg.reserve(feature.size() + build_flag.size() + k_fixed_overhead);
            msg.append("feature '");
            msg.append(feature);
            msg.append("' unavailable: built without ");
            msg.append(build_flag);
            return msg;
        }()) {}
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_FEATURE_UNAVAILABLE_HPP_
