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

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINDOWS
#else
#if defined(__CYGWIN__)
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <errno.h>
#include <microhttpd.h>
#ifdef HAVE_WEBSOCKET
#include <microhttpd_ws.h>
#endif  // HAVE_WEBSOCKET
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/body.hpp"
#include "httpserver/detail/route_tier.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif  // HAVE_GNUTLS

using std::string;
using std::pair;
using std::vector;
using std::map;
using std::set;

namespace httpserver {

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;

// Input-validation guard for on_methods_. Extracted so on_methods_ stays
// inside the project-wide CCN gate.
void webserver::validate_on_methods_inputs_(method_set methods,
        const std::string& path,
        const std::function<http_response(const http_request&)>& handler) const {
    if (methods.empty()) {
        throw std::invalid_argument(
            "route(method_set, ...) requires at least one method bit set");
    }
    if (!handler) {
        throw std::invalid_argument(
            "The handler function passed to on_*/route must be non-empty");
    }
    // Same single-resource constraint as register_path: only "" or "/"
    // is acceptable, and the matching mode must be exact (which on_*/
    // route are). Note: register_impl_ has an additional `!family` arm
    // in its guard (`single_resource && (...) || !family)`) that is always
    // true for on_* because on_*/route always use exact (non-prefix) matching
    // — omitting it here is intentional, not an oversight.
    //
    // Security note (CWE-284): lambda routes registered via on_*/route are
    // ALWAYS exact (family=false) by design. The `family` path-prefix
    // constraint enforced by register_impl_() at line 111 of
    // webserver_register.cpp does NOT apply here; prefix-matching is only
    // available via register_prefix(). The guard below is therefore
    // complete: path must be "" or "/" in single_resource mode, and the
    // route is always exact-matched — no additional family check is needed.
    if (single_resource && path != "" && path != "/") {
        throw std::invalid_argument(
            "When using a single_resource server, on_*/route requires "
            "the path to be '' or '/'");
    }
    // Lightweight input hygiene (CWE-20): reject paths containing embedded
    // null bytes. A std::string can hold '\0' but the underlying regex and
    // routing engines treat it as a string terminator, producing silent
    // mismatches. Reject early with a clear diagnostic rather than letting
    // the error surface deep in regex compilation or route matching.
    if (path.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "Route path must not contain embedded null bytes");
    }
}

void webserver::on_methods_(method_set methods,
                            const std::string& path,
                            std::function<http_response(const http_request&)> handler) {
    validate_on_methods_inputs_(methods, path, handler);

    detail::http_endpoint idx(path, /*family=*/false,
                              /*registration=*/true, regex_checking);

    bool is_new_entry = false;
    std::shared_ptr<detail::lambda_resource> shim;
    {
        // TASK-067: single-locked window across the v2 conflict probe,
        // the per-method atomicity pre-check, the slot writes, and the
        // table mutation. Prior to TASK-067 a separate registered_resources
        // mutex covered the prepare/commit/insert phase and was released
        // before the v2 upsert; the gap was harmless only because the
        // post-throw rollback unwound the v1 inserts.  With the v1 maps
        // gone, the route_table_mutex_ is the only consistency boundary
        // and must cover both the probe and the mutation. An upsert throw
        // (e.g. reject_terminus_collision) now leaves the local shim
        // unreferenced and discarded -- no rollback required since the
        // table itself was never touched.
        std::unique_lock table_lock(impl_->route_table_mutex_);
        shim = impl_->prepare_or_create_lambda_shim(idx, methods, is_new_entry);
        impl_->commit_handlers_to_shim(*shim, methods, std::move(handler));
        impl_->upsert_v2_table_entry_locked_(idx, methods, shim, is_new_entry);
    }
    impl_->invalidate_route_cache();
}

// The seven named forwarders below are the only place that maps the
// method name to its http_method enum constant. Each is a thin alias
// for on_methods_; all validation and insertion logic lives there.
// on_delete uses http_method::del because `delete` is a C++ keyword;
// the wire token is "DELETE" (see http_method::to_string).
void webserver::on_get(const std::string& path,
                       std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::get), path, std::move(handler));
}

void webserver::on_post(const std::string& path,
                        std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::post), path, std::move(handler));
}

void webserver::on_put(const std::string& path,
                       std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::put), path, std::move(handler));
}

void webserver::on_delete(const std::string& path,
                          std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::del), path, std::move(handler));
}

void webserver::on_patch(const std::string& path,
                         std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::patch), path, std::move(handler));
}

void webserver::on_options(const std::string& path,
                           std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::options), path, std::move(handler));
}

void webserver::on_head(const std::string& path,
                        std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::head), path, std::move(handler));
}

// TASK-026: generic table-driven entry points. The single-method form
// rejects http_method::count_ explicitly because the public route()
// overload accepts a runtime value (and so the sentinel is reachable);
// the on_* forwarders never pass count_, so the on_methods_ helper
// itself does not guard against it.
void webserver::route(http_method m,
                      const std::string& path,
                      std::function<http_response(const http_request&)> handler) {
    if (m == http_method::count_) {
        throw std::invalid_argument(
            "http_method::count_ is a sentinel and may not be "
            "registered as a route");
    }
    on_methods_(method_set{}.set(m), path, std::move(handler));
}

void webserver::route(method_set methods,
                      const std::string& path,
                      std::function<http_response(const http_request&)> handler) {
    // method_set::set() does not validate its argument; a caller who
    // passes only count_ bits produces a non-empty bitmask (the
    // count_ bit lies outside 0..count_-1) that for_each_requested_method
    // will iterate over zero times, resulting in an empty shim. The
    // on_methods_ empty() guard uses bits==0 and therefore does not catch
    // this edge case. The single-method route(http_method,...) already
    // guards against http_method::count_ explicitly; for the method_set
    // overload we rely on the documented precondition that callers only
    // set valid method bits. No additional sentinel check is added here
    // because method_set is a user-visible type and validating its
    // internal representation would duplicate policy already owned by
    // method_set itself.
    on_methods_(methods, path, std::move(handler));
}

// TASK-035: canonical smart-pointer overload. The templated unique_ptr
// shim in webserver.hpp constructs a shared_ptr from the unique_ptr and
// forwards here, so this is the single funnel for both ownership shapes.
void webserver::register_ws_resource(const std::string& resource,
                                     std::shared_ptr<websocket_handler> handler) {
#ifdef HAVE_WEBSOCKET
    if (!handler) {
        throw std::invalid_argument("The websocket_handler pointer cannot be null");
    }
    std::string url_key = http_utils::standardize_url(resource);
    std::unique_lock lock(impl_->registered_ws_handlers_mutex_);
    auto result = impl_->registered_ws_handlers.emplace(std::move(url_key),
                                                        std::move(handler));
    if (!result.second) {
        // Mirror TASK-023's throw-on-duplicate. v1's operator[]-based
        // insert silently overwrote; v2.0 surfaces the collision.
        throw std::invalid_argument(
            "A websocket_handler is already registered at this path");
    }
#else
    // TASK-034 §7: WebSocket compiled out -- fail loudly at the public
    // entry point so callers can catch feature_unavailable.
    (void)resource;
    (void)handler;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

void webserver::unregister_ws_resource(const std::string& resource) {
#ifdef HAVE_WEBSOCKET
    std::unique_lock lock(impl_->registered_ws_handlers_mutex_);
    impl_->registered_ws_handlers.erase(http_utils::standardize_url(resource));
#else
    (void)resource;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

}  // namespace httpserver
