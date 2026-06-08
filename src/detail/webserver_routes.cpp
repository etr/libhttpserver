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
// http_method enum-declaration order, which is also the serialization
// order for the `Allow:` header.
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

// Returns the route_entry that maps to @p idx in the v2 route table, or
// nullptr if no such entry exists. Probes the three tiers in the same
// order as lookup_v2 (exact -> radix -> regex) but matches on the
// canonical path key (idx.get_url_complete()) rather than a request URL,
// because the on_*/route conflict-detection oracle wants "is there
// already an entry registered AT this path", not "does this request URL
// match a registered route".
//
// Caller must hold route_table_mutex_ (any lock kind). The returned
// pointer is valid only while that lock is held. The pointer is `const`
// because the caller only reads handler/methods to extract the
// lambda_resource shim; the shim itself is mutated through its own
// shared_ptr<>, not through this route_entry.
const detail::route_entry* webserver_impl::find_v2_entry_by_path_(
        const detail::http_endpoint& idx) const noexcept {
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        // Parameterized routes live in the radix tier exclusively.
        // Probe the exact terminus at @p key (is_prefix=false); ignore
        // any prefix terminus -- on_*/route never reach this code with
        // a prefix endpoint (family=false at construction).
        detail::radix_match<detail::route_entry> rm;
        if (param_and_prefix_routes_.find(key, rm)
                && rm.entry != nullptr && !rm.is_prefix_match) {
            return rm.entry;
        }
        return nullptr;
    }
    // Non-parameterized: probe exact tier first, then regex tier by
    // url_complete string equality.
    auto it = exact_routes_.find(key);
    if (it != exact_routes_.end()) return &it->second;
    if (idx.is_regex_compiled()) {
        for (const auto& rr : regex_routes_) {
            if (rr.url_complete == key) return &rr.entry;
        }
    }
    return nullptr;
}

// Caller must hold route_table_mutex_ (unique_lock). The shim returned
// here is the SAME object that subsequent helpers (commit_handlers_to_shim,
// upsert_v2_table_entry_locked_) mutate; holding the lock across the
// whole prepare->commit->upsert sequence prevents a concurrent registration
// from racing in between.
std::shared_ptr<detail::lambda_resource>
webserver_impl::prepare_or_create_lambda_shim(const detail::http_endpoint& idx,
                                              method_set methods,
                                              bool& fresh_out) {
    const detail::route_entry* existing = find_v2_entry_by_path_(idx);
    if (existing == nullptr) {
        fresh_out = true;
        return std::make_shared<detail::lambda_resource>();
    }
    // Existing entry. route_entry::handler is a shared_ptr<http_resource>
    // (TASK-071 collapsed the prior variant). Dynamic-cast to
    // lambda_resource: if the cast misses, a class-based
    // register_path/register_prefix owns this path and we must throw.
    auto shim = std::dynamic_pointer_cast<detail::lambda_resource>(
        existing->handler);
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
        std::function<::httpserver::http_response(
            const ::httpserver::http_request&)> handler) {
    // Collect the set of requested methods in iteration order so the
    // loop below can identify the last one. Using a small inline array
    // avoids a heap allocation in the common case (N <= 9 methods).
    //
    // Move-into-last-slot optimisation (performance-reviewer-iter1-1):
    // all but the last slot receive a copy; the last slot is populated
    // by moving the handler, avoiding one extra heap allocation when the
    // std::function's capture is too large for the SBO buffer.
    std::array<http_method, static_cast<std::size_t>(http_method::count_)> buf{};
    std::size_t count = 0;
    for_each_requested_method(methods, [&](http_method m) {
        buf[count++] = m;
    });
    for (std::size_t i = 0; i < count; ++i) {
        if (i + 1 < count) {
            shim.set_slot(buf[i], handler);          // copy
        } else {
            shim.set_slot(buf[i], std::move(handler));  // move into last slot
        }
    }
}

void webserver_impl::reject_terminus_collision(const std::string& key,
        bool want_is_prefix) {
    // The route-cache key (method, path) cannot distinguish between an
    // exact_terminus_ and a prefix_terminus_ at the same path, so the
    // tiers must agree on the polarity at each canonical key. Probe
    // BOTH storage locations for an existing entry of the OPPOSITE
    // kind:
    //   - want_is_prefix=true (new prefix): refuse if there's an exact
    //     entry at `key` (either in exact_routes_ for unparameterized
    //     paths, or as a radix exact_terminus_ for parameterized ones).
    //   - want_is_prefix=false (new exact): refuse if there's a prefix
    //     entry at `key` (radix prefix_terminus_ only — there is no
    //     exact-tier storage for prefix routes).
    //
    // Throws BEFORE any mutation so the atomicity guarantee pinned by
    // upsert_param_route_failed_duplicate_leaves_original_intact holds
    // for the new throw too.
    const bool collision = !want_is_prefix
        ? param_and_prefix_routes_.has_terminus_at(key, /*is_prefix=*/true)
        : (exact_routes_.find(key) != exact_routes_.end()
           || param_and_prefix_routes_.has_terminus_at(
                  key, /*is_prefix=*/false));
    if (collision) {
        const char* incoming_kind = want_is_prefix ? "prefix" : "exact";
        const char* existing_kind = want_is_prefix ? "exact" : "prefix";
        throw std::invalid_argument(
            "Path '" + key + "' is already registered as a "
            + existing_kind
            + " route; cannot also register it as a "
            + incoming_kind
            + " route (the (method, path) cache key cannot "
              "discriminate the two)");
    }
}

void webserver_impl::upsert_v2_radix_route(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // TASK-056: refuse to plant an exact terminus on a node that
    // already carries a prefix terminus (or vice versa via the
    // symmetric guard in register_v2_route). Must run BEFORE the
    // read-merge below so a thrown exception leaves the table intact.
    reject_terminus_collision(key, /*want_is_prefix=*/false);
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

// Construct a non-prefix route_entry. Single helper for the two
// branches of insert_fresh_v2_entry that build the same 3-field shape.
// register_v2_route (webserver_register.cpp) constructs with set_all() +
// caller-controlled is_prefix and stays open-coded; update_existing_v2_entry
// merges into a target rather than constructing fresh.
static detail::route_entry make_non_prefix_entry(
        method_set methods, std::shared_ptr<http_resource> shim) {
    return detail::route_entry{methods, std::move(shim), /*is_prefix=*/false};
}

void webserver_impl::insert_fresh_v2_entry(const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim) {
    auto tier = classify_route_tier(idx);
    switch (tier.kind) {
    case route_tier_kind::radix:
        // Precondition: upsert_v2_table_entry routes url_pars-non-empty paths
        // through upsert_v2_radix_route before calling insert_fresh_v2_entry.
        assert(!"unreachable: radix paths go through upsert_v2_radix_route");
        __builtin_unreachable();
    case route_tier_kind::exact:
        // TASK-056: refuse to plant an exact entry when a prefix entry
        // for the same canonical path already lives in the radix tier.
        reject_terminus_collision(idx.get_url_complete(),
                                  /*want_is_prefix=*/false);
        exact_routes_.emplace(idx.get_url_complete(),
                              make_non_prefix_entry(methods, std::move(shim)));
        break;
    case route_tier_kind::pattern:
        // Regex-tier routes do not conflict with prefix routes because
        // a literal pattern with regex metacharacters is its own key
        // (it never matches as a prefix lookup target).
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re),
             make_non_prefix_entry(methods, std::move(shim))});
        break;
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
        if (rr.entry.handler == shim) {
            merge_into(rr.entry);
            return;
        }
    }
}

void webserver_impl::upsert_v2_table_entry_locked_(
        const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim, bool fresh) {
    // TASK-067: caller must already hold route_table_mutex_ (unique_lock).
    // The single-lock window covers the conflict probe (carried out by
    // prepare_or_create_lambda_shim via find_v2_entry_by_path_) all the
    // way through the table mutation here, so no concurrent registration
    // can race in between.
    //
    // TASK-027: mirror into the v2 3-tier table. We store the
    // lambda_resource shim via the shared_ptr arm so dispatch is
    // identical to class-resource registration. The methods bitmask
    // accumulates across calls when fresh==false.
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        upsert_v2_radix_route(key, methods, std::move(shim));
    } else if (fresh) {
        insert_fresh_v2_entry(idx, methods, std::move(shim));
    } else {
        update_existing_v2_entry(key, methods, std::move(shim));
    }
}

}  // namespace detail

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
