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


// ----- Resource registration --------------------------------------------

// register_path / register_prefix split. Both public methods funnel into
// this private helper, which carries the validation and insertion logic.
// Keeping the work in one place prevents drift between the two registration
// kinds.
// Input-validation guard for register_impl_. Extracted so register_impl_
// stays inside the project-wide CCN gate.
void webserver::validate_register_inputs_(const std::string& resource,
        const std::shared_ptr<http_resource>& res, bool family) const {
    if (res == nullptr) {
        throw std::invalid_argument("The http_resource pointer cannot be null");
    }
    if (single_resource && ((resource != "" && resource != "/") || !family)) {
        throw std::invalid_argument(
            "The resource should be '' or '/' and be registered via "
            "register_prefix when using a single_resource server");
    }
    // Lightweight input hygiene (CWE-20): reject paths containing embedded
    // null bytes, matching validate_on_methods_inputs_'s guard for on_*/
    // route. A std::string can hold '\0' but the underlying regex and
    // routing engines treat it as a string terminator, producing silent
    // mismatches. Reject early with a clear diagnostic rather than letting
    // the error surface deep in regex compilation or route matching.
    if (resource.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "Route path must not contain embedded null bytes");
    }
}

void webserver::register_impl_(const std::string& resource,
                               std::shared_ptr<http_resource> res,
                               bool family) {
    validate_register_inputs_(resource, res, family);

    detail::http_endpoint idx(resource, family, true, regex_checking);

    // Duplicate detection happens entirely
    // inside register_v2_route, which takes its own unique_lock on
    // route_table_mutex_ and runs reject_terminus_collision (plus a
    // tier-classification probe for the exact tier) BEFORE any mutation.
    // A duplicate at any tier surfaces as std::invalid_argument with the
    // table still in its prior state -- no rollback required.
    //
    // Ownership contract: when register_v2_route throws,
    // the shared_ptr parameter `res` (and any unique_ptr-derived shared_ptr
    // funnelled through the webserver.hpp inline template shim) is destroyed
    // by exception unwinding, cleanly releasing the resource.
    impl_->register_v2_route(idx, std::move(res), family);
    impl_->invalidate_route_cache();
}

namespace detail {

// Helper used by register_v2_route. Probes the v2 tiers for a
// pre-existing registration at @p idx and throws std::invalid_argument
// before any mutation if one exists. This is the duplicate-detection
// oracle for registration.
//
// Caller must hold route_table_mutex_ (unique_lock). The reject_terminus_
// collision guard (prefix-vs-exact polarity) is run separately in
// register_v2_route.
namespace {
[[noreturn]] void throw_duplicate_registration(const std::string& key) {
    throw std::invalid_argument(
        "A resource is already registered at this path: '" + key + "'");
}
}  // namespace

void webserver_impl::reject_duplicate_v2_entry_(
        const detail::http_endpoint& idx, bool family) {
    const std::string& key = idx.get_url_complete();
    if (family) {
        if (param_and_prefix_routes_.has_terminus_at(key, /*is_prefix=*/true)) {
            throw_duplicate_registration(key);
        }
        return;
    }
    auto pre_tier = classify_route_tier(idx);
    switch (pre_tier.kind) {
    case route_tier_kind::exact:
        if (exact_routes_.find(key) != exact_routes_.end()) {
            throw_duplicate_registration(key);
        }
        break;
    case route_tier_kind::radix:
        if (param_and_prefix_routes_.has_terminus_at(
                key, /*is_prefix=*/false)) {
            throw_duplicate_registration(key);
        }
        break;
    case route_tier_kind::pattern:
        for (const auto& rr : regex_routes_) {
            if (rr.url_complete == key) throw_duplicate_registration(key);
        }
        break;
    }
}

void webserver_impl::register_v2_route(const detail::http_endpoint& idx,
        std::shared_ptr<http_resource> res, bool family) {
    // Place a register_path / register_prefix registration into the
    // v2 3-tier route table. Tier placement via classify_route_tier()
    // (single source-of-truth):
    //   - family=true  -> radix tree (prefix terminus).
    //   - radix tier   -> radix tree (exact terminus, wildcard nodes).
    //   - regex tier   -> regex_routes_ (pre-compiled at registration time).
    //   - exact tier   -> exact_routes_ hash map.
    std::unique_lock table_lock(route_table_mutex_);
    // Guard against prefix-vs-exact terminus collisions on
    // the canonical key. Run BEFORE any mutation so the throw leaves
    // the route table in its prior state.
    reject_terminus_collision(idx.get_url_complete(),
                              /*want_is_prefix=*/family);
    // Same-kind duplicate detection. Throws BEFORE any mutation so the
    // atomicity contract pinned by basic_suite::duplicate_endpoints
    // holds: a failed registration leaves the table exactly as before.
    reject_duplicate_v2_entry_(idx, family);
    detail::route_entry entry;
    entry.methods = method_set{}.set_all();
    entry.handler = std::move(res);
    entry.is_prefix = family;
    if (family) {
        param_and_prefix_routes_.insert(idx.get_url_complete(), std::move(entry),
                                        /*is_prefix=*/true);
        return;
    }
    auto tier = classify_route_tier(idx);
    switch (tier.kind) {
    case route_tier_kind::radix:
        param_and_prefix_routes_.insert(idx.get_url_complete(), std::move(entry),
                                        /*is_prefix=*/false);
        break;
    case route_tier_kind::pattern:
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re), std::move(entry)});
        break;
    case route_tier_kind::exact:
        exact_routes_.emplace(idx.get_url_complete(), std::move(entry));
        break;
    }
}

}  // namespace detail

void webserver::register_path(const std::string& path,
                              std::shared_ptr<http_resource> res) {
    register_impl_(path, std::move(res), /*family=*/false);
}

void webserver::register_prefix(const std::string& path,
                                std::shared_ptr<http_resource> res) {
    register_impl_(path, std::move(res), /*family=*/true);
}

// Erase a single registration of the requested kind (family).
// Each kind keeps a distinct v2-table entry (parameterized routes live in
// the radix tier, regex routes in the regex_routes_ vector, exact routes
// in the exact_routes_ hash map; prefix routes are radix-tier with
// is_prefix=true), so we route by the same classification used at
// registration. The route_table_mutex_ write lock keeps the erasure
// atomic against concurrent dispatch.
void webserver::unregister_impl_(const string& resource, bool family) {
    detail::http_endpoint he(resource, family, true, regex_checking);

    // Erase from the v2 3-tier table.
    // Lock ordering: route_table_mutex_ -> route_lru_cache's internal
    // mutex (inside invalidate_route_cache). Table lock released before
    // the LRU cache is cleared, matching register_impl_ / on_methods_.
    {
        std::unique_lock table_lock(impl_->route_table_mutex_);
        const std::string& key = he.get_url_complete();
        if (family) {
            impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/true);
        } else if (!he.get_url_pars().empty()) {
            impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/false);
        } else {
            // Erase from exact tier; also sweep regex tier (url_complete key).
            impl_->exact_routes_.erase(key);
            impl_->regex_routes_.erase(
                std::remove_if(impl_->regex_routes_.begin(),
                               impl_->regex_routes_.end(),
                               [&key](const detail::webserver_impl::regex_route& rr) {
                                   return rr.url_complete == key;
                               }),
                impl_->regex_routes_.end());
        }
    }
    impl_->invalidate_route_cache();
}

void webserver::unregister_path(const string& path) {
    unregister_impl_(path, /*family=*/false);
}

void webserver::unregister_prefix(const string& path) {
    unregister_impl_(path, /*family=*/true);
}

void webserver::unregister_resource(const string& resource) {
    // Build the canonical endpoint key once. The family flag does not
    // affect which v2 storage location holds the entry; the
    // url_complete key is the only sweep key.
    detail::http_endpoint he_exact(resource, /*family=*/false, true, regex_checking);

    // Erase from the v2 3-tier table. Hold a single write-lock
    // across all four tier sweeps so no request thread can observe a
    // partially-unregistered state (CWE-367 TOCTOU: a prior register_path
    // AND register_prefix on the same path are both cleared atomically).
    {
        std::unique_lock table_lock(impl_->route_table_mutex_);
        const std::string& key = he_exact.get_url_complete();
        impl_->exact_routes_.erase(key);
        impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/false);
        impl_->param_and_prefix_routes_.remove(key, /*is_prefix=*/true);
        // Also sweep the regex tier by url_complete.
        impl_->regex_routes_.erase(
            std::remove_if(impl_->regex_routes_.begin(),
                           impl_->regex_routes_.end(),
                           [&key](const detail::webserver_impl::regex_route& rr) {
                               return rr.url_complete == key;
                           }),
            impl_->regex_routes_.end());
    }
    // Delegate cache clearing to invalidate_route_cache() matching the
    // pattern used by register_impl_ and on_methods_ (table lock released
    // before cache is cleared).
    impl_->invalidate_route_cache();
}

// IP-control API: a symmetric, consistently-named surface (replacing
// the v1 ban_ip / unban_ip / allow_ip / disallow_ip quartet):
//   deny_ip / remove_denied_ip   -> the deny list (exception under ACCEPT)
//   allow_ip / remove_allowed_ip -> the allow list (exception under REJECT;
//                                   also overrides a deny entry under ACCEPT)
// See webserver::deny_ip / allow_ip (impls in webserver_setup.cpp) and
// classify_decision (webserver_callbacks_lifecycle.cpp).

}  // namespace httpserver
