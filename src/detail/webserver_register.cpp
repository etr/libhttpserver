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
void webserver::register_impl_(const std::string& resource,
                               std::shared_ptr<http_resource> res,
                               bool family) {
    if (res == nullptr) {
        throw std::invalid_argument("The http_resource pointer cannot be null");
    }

    if (single_resource && ((resource != "" && resource != "/") || !family)) {
        throw std::invalid_argument("The resource should be '' or '/' and be registered via register_prefix when using a single_resource server");
    }

    detail::http_endpoint idx(resource, family, true, regex_checking);

    std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);
    auto result = impl_->registered_resources.insert({idx, res});

    if (!result.second) {
        // TASK-023: v1 returned false on duplicate. The new void API
        // throws so the caller cannot silently lose ownership. For the
        // unique_ptr overload the resource was moved into a shared_ptr by
        // the inline template shim in webserver.hpp before this function
        // was entered; it will be destroyed when the shared_ptr parameter
        // 'res' is unwound by this exception, cleanly releasing ownership.
        throw std::invalid_argument(
            "A resource is already registered at this path");
    }

    // is_plain_path: true when the route has no wildcards of either kind
    // (not a family/prefix route AND no parameterized URL segments). Only
    // plain-path routes go into the fast string-keyed lookup map.
    bool is_plain_path = !family && idx.get_url_pars().empty();
    if (is_plain_path) {
        impl_->registered_resources_str.insert(
            {idx.get_url_complete(), result.first->second});
    }
    if (idx.is_regex_compiled()) {
        // res is also passed to register_v2_route below; copy the shared_ptr
        // here (one extra ref-count increment) rather than moving it, since
        // shared_ptr copies are cheaper than the regex scan they help avoid.
        impl_->registered_resources_regex.insert({idx, res});
    }
    // Release the registration mutex before clearing the LRU cache.
    // route_lru_cache.clear() takes the cache's internal mutex; the
    // lock order documented in webserver_impl.hpp acquires the table
    // mutex BEFORE the cache mutex and never both at once, so the
    // registration mutex (which gates the registration-side maps,
    // distinct from the v2 table mutex) is also released first as a
    // matter of discipline.
    registered_resources_lock.unlock();
    // A reader can transiently see the new entry without a warm cache,
    // causing one extra tier walk on the first hit — harmless read-stale
    // effect: the resource is already visible under the shared lock.

    // TASK-056: register_v2_route may throw std::invalid_argument when a
    // prefix-vs-exact terminus collision is detected on the canonical
    // path. If it does, undo the v1 inserts above so the table stays
    // consistent with the caller's mental model ("the call threw, so
    // nothing was registered"). Locks are reacquired briefly for the
    // rollback; the registration was visible to readers in the window
    // between, which is the same harmless read-stale effect documented
    // for the cache invalidation comment above.
    try {
        impl_->register_v2_route(idx, std::move(res), family);
    } catch (...) {
        std::unique_lock rollback_lock(impl_->registered_resources_mutex);
        impl_->registered_resources.erase(idx);
        if (is_plain_path) {
            impl_->registered_resources_str.erase(idx.get_url_complete());
        }
        if (idx.is_regex_compiled()) {
            impl_->registered_resources_regex.erase(idx);
        }
        throw;
    }
    impl_->invalidate_route_cache();
}

namespace detail {

void webserver_impl::register_v2_route(const detail::http_endpoint& idx,
        std::shared_ptr<http_resource> res, bool family) {
    // TASK-027: mirror a register_path / register_prefix call into the
    // v2 3-tier route table. Tier placement via classify_route_tier()
    // (single source-of-truth):
    //   - family=true  -> radix tree (prefix terminus).
    //   - radix tier   -> radix tree (exact terminus, wildcard nodes).
    //   - regex tier   -> regex_routes_ (pre-compiled at registration time).
    //   - exact tier   -> exact_routes_ hash map.
    std::unique_lock table_lock(route_table_mutex_);
    // TASK-056: guard against prefix-vs-exact terminus collisions on
    // the canonical key. Run BEFORE any mutation so the throw leaves
    // the route table in its prior state. (See
    // reject_terminus_collision for the full rationale.)
    reject_terminus_collision(idx.get_url_complete(),
                              /*want_is_prefix=*/family);
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

// Deprecated forwarder kept for backward compatibility. Users that want
// prefix matching must call register_prefix().
void webserver::register_resource(const std::string& resource,
                                  std::shared_ptr<http_resource> res) {
    register_path(resource, std::move(res));
}

void webserver::unregister_impl_(const string& resource, bool family) {
    detail::http_endpoint he(resource, family, true, regex_checking);

    {
        std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);
        impl_->registered_resources.erase(he);
        // Prefix routes are never stored in the regex chain (only paths with
        // regex metacharacters go there, and register_prefix does not set
        // is_regex_compiled). The erase below is therefore a safe no-op for
        // the family=true path but is harmless to call unconditionally.
        impl_->registered_resources_regex.erase(he);
        // The string-keyed fast-path map only ever holds exact (non-family)
        // entries (see register_impl_). A prefix unregister has nothing to
        // do here.
        if (!family) {
            impl_->registered_resources_str.erase(he.get_url_complete());
        }
    }

    // TASK-027: mirror the erasure into the v2 3-tier table.
    // Lock ordering: registered_resources_mutex (released above) ->
    // route_table_mutex_ -> route_lru_cache's internal mutex
    // (inside invalidate_route_cache). Table lock released before the
    // LRU cache is cleared, matching register_impl_ / on_methods_.
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
    // Build both endpoint keys before acquiring any lock.
    detail::http_endpoint he_exact(resource, /*family=*/false, true, regex_checking);
    detail::http_endpoint he_prefix(resource, /*family=*/true,  true, regex_checking);

    {
        // Hold a single write-lock across both v1 erasures AND the cache
        // key removal below so no request thread can observe a
        // partially-unregistered state (CWE-367 TOCTOU fix: the exact entry
        // and the prefix entry are removed atomically).
        std::unique_lock registered_resources_lock(impl_->registered_resources_mutex);
        impl_->registered_resources.erase(he_exact);
        impl_->registered_resources.erase(he_prefix);
        impl_->registered_resources_regex.erase(he_exact);
        impl_->registered_resources_regex.erase(he_prefix);
        // The string-keyed fast-path map only holds exact (non-family) entries.
        impl_->registered_resources_str.erase(he_exact.get_url_complete());
    }

    // TASK-027: mirror into the v2 3-tier table. Erase under both
    // classifications so a prior register_path AND register_prefix on
    // the same path are both cleared atomically.
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

// TASK-029: The v2.0 public IP-control API is the pair block_ip / unblock_ip.
// The historical ban_ip / unban_ip / allow_ip / disallow_ip quartet was
// collapsed to a single name pair operating on the deny list. The internal
// allowances set and the allow-list branch in policy_callback remain in
// place so default_policy(REJECT) keeps working at the daemon level, but

}  // namespace httpserver
