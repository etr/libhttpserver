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

using detail::classify_route_tier;
using detail::route_tier_kind;
using detail::route_tier_result;


//   2. Looks up any existing entry at the path. If it's a class-based
//      http_resource, throw -- lambda and class registrations cannot
//      share a path. If it's an existing lambda_resource shim, check
//      that EVERY requested method slot is empty before mutating any
//      of them (atomic all-or-nothing); otherwise throw.
//   3. If no entry exists, build a fresh lambda_resource shim and
//      insert it into the same three storage maps used by
//      register_impl_ (master ordered map; the str fast-path map iff
//      exact non-parameterized; the regex map iff parameterized).
//   4. Write @p handler into each requested method slot.
//   5. Invalidate the LRU route cache.
//
// The dispatch path in finalize_answer is not modified: it already
// looks up via shared_ptr<http_resource>, calls is_allowed(method)
// gating the 405 path, then dispatches via the per-method member-
// function pointer set in answer_to_connection. The lambda_resource
// shim's render_* overrides invoke the stored slot.
namespace {

// Iterate enum-declaration order (get, head, post, ...) over the bits
// set in @p methods, invoking @p fn for each. Used by the on_methods_
// pre-check loop and the commit loop; pulling the scaffolding into a
// single helper dedupes the iteration boilerplate. The order matches
// http_method::count_'s enum order (TASK-021), which is also the
// serialization order for the `Allow:` header.
template <typename Fn>
void for_each_requested_method(method_set methods, Fn&& fn) {
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (methods.contains(m)) fn(m);
    }
}

}  // namespace

namespace detail {

std::shared_ptr<detail::lambda_resource>
webserver_impl::prepare_or_create_lambda_shim(const detail::http_endpoint& idx,
                                              method_set methods,
                                              bool& fresh_out) {
    auto it = registered_resources.find(idx);
    if (it == registered_resources.end()) {
        fresh_out = true;
        return std::make_shared<detail::lambda_resource>();
    }
    // Existing entry. Must be a lambda_resource shim, otherwise a
    // class-based register_path/register_prefix has already taken
    // this path -- lambda and class registrations cannot coexist
    // on the same path.
    auto shim = std::dynamic_pointer_cast<detail::lambda_resource>(it->second);
    if (!shim) {
        throw std::invalid_argument(
            "A non-lambda http_resource is already registered at "
            "this path; on_*/route cannot share a path with "
            "register_path/register_prefix");
    }
    // Atomicity pre-check: every requested slot must be empty BEFORE
    // we mutate any of them.
    for_each_requested_method(methods, [&](http_method m) {
        if (shim->has_slot(m)) {
            throw std::invalid_argument(
                "A handler is already registered for one of the "
                "requested methods on this path");
        }
    });
    fresh_out = false;
    return shim;
}

void webserver_impl::commit_handlers_to_shim(detail::lambda_resource& shim,
        method_set methods,
        const std::function<::httpserver::http_response(
            const ::httpserver::http_request&)>& handler) {
    // The shared std::function copies cheaply (type-erased callable),
    // so each slot owns its own copy.
    for_each_requested_method(methods, [&](http_method m) {
        shim.set_slot(m, handler);
    });
}

void webserver_impl::insert_fresh_v1_entries(const detail::http_endpoint& idx,
        std::shared_ptr<http_resource> shim) {
    registered_resources.insert({idx, shim});
    if (idx.get_url_pars().empty()) {
        registered_resources_str.insert({idx.get_url_complete(), shim});
    }
    if (idx.is_regex_compiled()) {
        registered_resources_regex.insert({idx, shim});
    }
}

void webserver_impl::upsert_v2_param_route(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // Read-merge-reinsert: radix_tree::insert always overwrites the
    // terminus, so we must fold any existing entry's methods in first.
    detail::radix_match<detail::route_entry> existing;
    detail::route_entry merged;
    if (param_and_prefix_routes_.find(key, existing)
            && existing.entry && !existing.is_prefix_match) {
        merged = *existing.entry;
    }
    merged.methods = merged.methods | methods;
    merged.handler = std::move(shim);
    merged.is_prefix = false;
    param_and_prefix_routes_.insert(key, std::move(merged), /*is_prefix=*/false);
}

void webserver_impl::insert_fresh_v2_entry(const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim) {
    auto tier = classify_route_tier(idx);
    switch (tier.kind) {
    case route_tier_kind::radix:
        // Unreachable: callers route url_pars-non-empty through
        // upsert_v2_param_route, never here.
        break;
    case route_tier_kind::exact: {
        detail::route_entry entry;
        entry.methods   = methods;
        entry.handler   = std::move(shim);
        entry.is_prefix = false;
        exact_routes_.emplace(idx.get_url_complete(), std::move(entry));
        break;
    }
    case route_tier_kind::regex_: {
        detail::route_entry entry;
        entry.methods   = methods;
        entry.handler   = std::move(shim);
        entry.is_prefix = false;
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re), std::move(entry)});
        break;
    }
    }
}

void webserver_impl::update_existing_v2_entry(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // The tier was fixed at first registration. For the exact tier a
    // direct map lookup suffices; for the regex tier walk the vector
    // and match by shim identity (regex patterns are not repeated
    // keys; pointer identity is the cheapest and most reliable
    // discriminator).
    auto merge_into = [&](detail::route_entry& target) {
        target.methods = target.methods | methods;
        target.handler = shim;
        target.is_prefix = false;
    };
    auto exact_it = exact_routes_.find(key);
    if (exact_it != exact_routes_.end()) {
        merge_into(exact_it->second);
        return;
    }
    for (auto& rr : regex_routes_) {
        auto* sp = std::get_if<std::shared_ptr<http_resource>>(&rr.entry.handler);
        if (sp && *sp == shim) {
            merge_into(rr.entry);
            return;
        }
    }
}

void webserver_impl::upsert_v2_table_entry(const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim, bool fresh) {
    // TASK-027: mirror into the v2 3-tier table. We store the
    // lambda_resource shim via the shared_ptr arm so dispatch is
    // identical to class-resource registration. The methods bitmask
    // accumulates across calls when fresh==false.
    std::unique_lock table_lock(route_table_mutex_);
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        upsert_v2_param_route(key, methods, std::move(shim));
    } else if (fresh) {
        insert_fresh_v2_entry(idx, methods, std::move(shim));
    } else {
        update_existing_v2_entry(key, methods, std::move(shim));
    }
}

}  // namespace detail

void webserver::on_methods_(method_set methods,
                            const std::string& path,
                            std::function<http_response(const http_request&)> handler) {
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
    // route are).
    if (single_resource && path != "" && path != "/") {
        throw std::invalid_argument(
            "When using a single_resource server, on_*/route requires "
            "the path to be '' or '/'");
    }

    detail::http_endpoint idx(path, /*family=*/false,
                              /*registration=*/true, regex_checking);

    bool fresh = false;
    std::shared_ptr<detail::lambda_resource> shim;
    {
        std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);
        shim = impl_->prepare_or_create_lambda_shim(idx, methods, fresh);
        impl_->commit_handlers_to_shim(*shim, methods, handler);
        if (fresh) impl_->insert_fresh_v1_entries(idx, shim);
    }

    impl_->upsert_v2_table_entry(idx, methods, shim, fresh);
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
    std::string key = http_utils::standardize_url(resource);
    std::unique_lock lock(impl_->registered_resources_mutex);
    auto result = impl_->registered_ws_handlers.emplace(std::move(key),
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
    std::unique_lock lock(impl_->registered_resources_mutex);
    impl_->registered_ws_handlers.erase(http_utils::standardize_url(resource));
#else
    (void)resource;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

}  // namespace httpserver
