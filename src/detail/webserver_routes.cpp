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
    // is acceptable. register_impl_'s version of this guard
    // (webserver_register.cpp) has an additional `!family` arm; it is
    // deliberately omitted here because on_*/route routes are ALWAYS
    // exact-matched (family=false by design -- prefix matching is only
    // available via register_prefix()), so the arm would always be
    // true. The guard below is therefore complete for lambda routes.
    if (config.single_resource && path != "" && path != "/") {
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
    // Hardening: registration is a privileged
    // server-setup call, not a network-reachable path, but an
    // accidentally megabyte-scale path would still be stored in the
    // route table data structures for no benefit. Reject unreasonably
    // long paths early with a clear diagnostic.
    if (path.size() > 8192) {
        throw std::invalid_argument(
            "Route path exceeds maximum length of 8192 bytes");
    }
}

void webserver::on_methods_(method_set methods,
                            const std::string& path,
                            std::function<http_response(const http_request&)> handler) {
    validate_on_methods_inputs_(methods, path, handler);

    detail::http_endpoint idx(path, /*family=*/false,
                              /*registration=*/true, config.regex_checking);

    {
        // Single-locked window across the v2 conflict probe, the
        // per-method atomicity pre-check, the slot writes, and the
        // table mutation. route_table_mutex_ is the only consistency
        // boundary and must cover both the probe and the mutation. An
        // upsert throw (e.g. reject_terminus_collision) leaves the
        // local shim unreferenced and discarded -- no rollback required
        // since the table itself was never touched.
        auto table_lock = impl_->routes_.lock_for_write();
        // is_new_entry is the bool prepare_or_create_lambda_shim
        // returns as /*fresh=*/ and upsert_v2_table_entry_locked_
        // receives as `fresh` -- the same flag under three names.
        auto [shim, is_new_entry] =
            impl_->prepare_or_create_lambda_shim(idx, methods);
        impl_->commit_handlers_to_shim(*shim, methods, std::move(handler));
        impl_->routes_.upsert_v2_table_entry_locked_(idx, methods, shim,
                                                     is_new_entry);
    }
    impl_->routes_.invalidate_route_cache();
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

// Generic table-driven entry points. The single-method form
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

// Canonical smart-pointer overload. The templated unique_ptr
// shim in webserver.hpp constructs a shared_ptr from the unique_ptr and
// forwards here, so this is the single funnel for both ownership shapes.
void webserver::register_ws_resource(const std::string& resource,
                                     std::shared_ptr<websocket_handler> handler) {
#ifdef HAVE_WEBSOCKET
    if (!handler) {
        throw std::invalid_argument("The websocket_handler pointer cannot be null");
    }
    std::string url_key = http_utils::standardize_url(resource);
    if (!impl_->ws_.try_register(std::move(url_key), std::move(handler))) {
        // v1's operator[]-based insert silently overwrote; v2.0
        // surfaces the collision by throwing.
        throw std::invalid_argument(
            "A websocket_handler is already registered at this path");
    }
#else
    // WebSocket compiled out -- fail loudly at the public
    // entry point so callers can catch feature_unavailable.
    (void)resource;
    (void)handler;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

void webserver::unregister_ws_resource(const std::string& resource) {
#ifdef HAVE_WEBSOCKET
    impl_->ws_.unregister(http_utils::standardize_url(resource));
#else
    (void)resource;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

}  // namespace httpserver
