/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// route_table.cpp -- the v2 3-tier route table (exact / radix / regex)
// fronted by an LRU cache. Extracted from webserver_impl so the
// coordinator is a thin holder of routing state rather than owning it.
// The lambda_resource shim-creation POLICY for on_*/route registration
// stays on webserver_impl (see prepare_or_create_lambda_shim in
// webserver_routes_upsert.cpp); only the containers, the lookup pipeline,
// and the locked upsert/reject primitives live here.

#include "httpserver/detail/route_table.hpp"

#include <cassert>
#include <memory>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/http_method.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/route_cache.hpp"
#include "httpserver/detail/route_entry.hpp"
#include "httpserver/detail/route_tier.hpp"
#include "httpserver/detail/segment_trie.hpp"

namespace httpserver {
namespace detail {

using detail::classify_route_tier;
using detail::route_tier_kind;
using detail::route_tier_result;

// ----------------------------------------------------------------------
// Lookup pipeline.
// ----------------------------------------------------------------------

// 3-tier route lookup pipeline (lookup_v2, defined below):
//   1. exact tier — transparent-map probe under a shared route-table
//      lock. Deliberately bypasses the LRU cache; the rationale lives
//      at the probe site inside lookup_v2.
//   2. parameter/regex LRU cache (cache mutex only) — return on hit,
//      promoting LRU.
//   3. on miss, take a shared_lock on route_table_mutex_:
//      a. param_and_prefix_routes_ (segment-trie)
//      b. regex_routes_ (linear scan)
//      then drop the table lock and install the result into the cache
//      (we never hold both locks simultaneously — the table lock is
//      released before the cache lock is taken).
//
// The method-set check (does the entry serve `method`?) lives at the
// dispatch site, NOT here, because the existing 405 + Allow: header
// path needs to see the entry even when no method bit matches.

// Canonicalize a lookup path the same way http_endpoint canonicalizes a
// registration path: strip a trailing '/' (unless the path IS just "/"),
// prepend '/' if missing. Registration stores keys under url_complete,
// which is produced by this same normalization (see http_endpoint.cpp
// ll. 60-67) — so lookup must canonicalize too or "/foo" and "/foo/"
// would never share an entry. Matches the v1 dispatch path, which
// constructs a non-registration http_endpoint at lookup time and so
// gets the same normalization for free.
//
// Returns a string_view so the happy path (input
// already canonical) allocates zero heap memory.  On the rewrite
// path the canonicalised form is written into the caller-owned
// @p scratch buffer and a view into that buffer is returned.
//
// LIFETIME: the returned view is valid for the duration of the
// call chain only; it points at either the immutable "/" literal,
// the caller's @p path argument, or the caller's @p scratch buffer.
// Any caller storing the view must copy it into an owning string
// first.  The cache layer (cache_key constructed below) already
// copies via std::string, so the contract is naturally respected.
static std::string_view canonicalize_lookup_path(
        std::string_view path, std::string& scratch) {
    if (path.empty()) {
        // Immutable canonical root -- no scratch usage.
        return std::string_view{"/", 1};
    }
    const bool has_leading_slash = (path.front() == '/');
    const bool has_trailing_slash = (path.size() > 1 && path.back() == '/');
    if (has_leading_slash && !has_trailing_slash) {
        // Already canonical: return the caller's view directly.
        return path;
    }
    // Rewrite path: write the canonicalised form into the caller's
    // scratch buffer and return a view into it.
    scratch.clear();
    scratch.reserve(path.size() + (has_leading_slash ? 0 : 1));
    if (!has_leading_slash) {
        scratch.push_back('/');
    }
    if (has_trailing_slash) {
        scratch.append(path.data(), path.size() - 1);
    } else {
        scratch.append(path.data(), path.size());
    }
    return std::string_view{scratch};
}

route_table::lookup_result
route_table::lookup_v2(http_method method, const std::string& path) {
    lookup_result result;

    // canonicalize_lookup_path returns a string_view
    // into either @p path (already-canonical happy path -- no
    // allocation), the immutable "/" literal (empty-input case), or
    // @p canonicalize_scratch (rewrite case -- single allocation,
    // bounded by the input size).
    std::string canonicalize_scratch;
    std::string_view lookup_path =
        canonicalize_lookup_path(path, canonicalize_scratch);

    // Step 1: exact tier, probed FIRST and under the route-table shared
    // lock only. exact_routes_ uses std::less<> (transparent), so the
    // string_view key needs no std::string allocation, and concurrent
    // worker threads read it in parallel with zero route_lru_cache
    // traffic.
    //
    // The exact tier deliberately BYPASSES route_lru_cache: fronting an
    // O(log n) transparent map probe with the cache would put every
    // request behind the cache's exclusive std::mutex plus an LRU splice
    // WRITE on each hit -- serialising the hottest dispatch path and
    // dirtying a shared cache line across the thread pool -- for no
    // lookup-cost saving. Only the parameter/regex tiers, whose match is
    // genuinely expensive, are cached below. This matches the v1 dispatch
    // model, where exact routes were a plain shared_lock map probe and
    // only regex results were memoised.
    {
        std::shared_lock table_lock(route_table_mutex_);
        auto exact_it = exact_routes_.find(lookup_path);
        if (exact_it != exact_routes_.end()) {
            result.found = true;
            result.tier = tier_hit::exact;
            result.entry = exact_it->second;
            // exact tier carries no parameters by definition.
            return result;
        }
    }

    // Step 2: parameter/regex cache. Cache under the canonical key so
    // /foo and /foo/ share an entry. find_by_view avoids copying
    // lookup_path into a cache_key on the warm path.
    cache_value cached;
    if (route_lru_cache.find_by_view(method, lookup_path, cached)) {
        result.found = true;
        result.tier = tier_hit::cache;
        result.entry = std::move(cached.entry);
        result.captured_params = std::move(cached.captured_params);
        return result;
    }

    // Step 3: cache miss -- walk the parameter/prefix (radix) then regex
    // tiers under the shared lock. Construct the owning cache_key once
    // here; the exact-hit and warm-cache paths never reach this line.
    cache_key key{method, std::string(lookup_path)};
    {
        std::shared_lock table_lock(route_table_mutex_);

        // Radix tier — segment-trie walk.
        segment_trie_match<route_entry> rm;
        if (param_and_prefix_routes_.find(key.path, rm) && rm.entry) {
            result.found = true;
            result.tier = tier_hit::radix;
            result.entry = *rm.entry;
            result.captured_params = std::move(rm.captures);
        }

        // Regex tier — linear scan over pre-compiled std::regex objects.
        // Patterns were compiled once at registration time (in register_v2_route
        // and the on_*/route upsert path), so no compilation cost is paid
        // per lookup.
        if (!result.found) {
            for (const auto& rr : regex_routes_) {
                if (std::regex_match(key.path, rr.compiled_re)) {
                    result.found = true;
                    result.tier = tier_hit::regex;
                    result.entry = rr.entry;
                    break;
                }
            }
        }
    }  // table_lock released.

    // Step 4: install radix/regex results into the cache. Copy (not
    // move) — the caller consumes `result` after this returns, and a
    // move would leave the shared_ptr variant arm null (false-negative
    // 404).
    if (result.found) {
        route_lru_cache.insert(
            key, cache_value{result.entry, result.captured_params});
    }

    return result;
}

void route_table::invalidate_route_cache() {
    // Called by registration callers after any table mutation.
    // Contract: caller must NOT hold route_table_mutex_ here -- the lock is
    // taken internally by route_cache::clear() (route_cache.hpp), and
    // holding route_table_mutex_ across this call is unnecessary and risks
    // a lock-ordering inversion with the 3-tier lookup path above.
    route_lru_cache.clear();
}

// ----------------------------------------------------------------------
// Registration probes + upsert machinery (caller holds route_table_mutex_
// unless the method locks internally, as noted per-method).
// ----------------------------------------------------------------------

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
const route_entry* route_table::find_v2_entry_by_path_(
        const http_endpoint& idx) const noexcept {
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        // Parameterized routes live in the radix tier exclusively.
        // Probe the exact terminus at @p key (is_prefix=false); ignore
        // any prefix terminus -- on_*/route never reach this code with
        // a prefix endpoint (family=false at construction).
        //
        // Note this feeds the radix tier's request-path find() a
        // registered PATTERN string. That works for the common case: a
        // literal "{name}" segment finds no exact child (insert stored
        // it as the wildcard child) and descends that unconstrained
        // wildcard child. A constrained "{id|[0-9]+}" segment, however,
        // fails the constraint regex_match in step_to_child_
        // (segment_trie.hpp) and is NOT found -- the literal pattern text
        // does not satisfy its own constraint.
        segment_trie_match<route_entry> rm;
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

void route_table::reject_terminus_collision(const std::string& key,
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
    bool collision = false;
    if (!want_is_prefix) {
        // New exact: refuse if a prefix entry already exists at this key.
        collision = param_and_prefix_routes_.has_terminus_at(
            key, /*is_prefix=*/true);
    } else {
        // New prefix: refuse if an exact entry already exists at this key.
        collision = exact_routes_.find(key) != exact_routes_.end()
            || param_and_prefix_routes_.has_terminus_at(
                   key, /*is_prefix=*/false);
    }
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

void route_table::upsert_v2_radix_route(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // Refuse to plant an exact terminus on a node that
    // already carries a prefix terminus (or vice versa via the
    // symmetric guard in register_v2_route). Must run BEFORE the
    // read-merge below so a thrown exception leaves the table intact.
    reject_terminus_collision(key, /*want_is_prefix=*/false);
    // Read-merge-reinsert: segment_trie::insert always overwrites the
    // terminus, so we must fold any existing entry's methods in first.
    segment_trie_match<route_entry> existing;
    route_entry merged;
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
// register_v2_route constructs with set_all() + caller-controlled
// is_prefix and stays open-coded; update_existing_v2_entry merges into a
// target rather than constructing fresh.
static route_entry make_non_prefix_entry(
        method_set methods, std::shared_ptr<http_resource> shim) {
    return route_entry{methods, std::move(shim), /*is_prefix=*/false};
}

void route_table::insert_fresh_v2_entry(const http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim) {
    auto tier = classify_route_tier(idx);
    switch (tier.kind) {
    case route_tier_kind::radix:
        // Precondition: upsert_v2_table_entry_locked_ routes
        // url_pars-non-empty paths through upsert_v2_radix_route before
        // calling insert_fresh_v2_entry.
        assert(!"unreachable: radix paths go through upsert_v2_radix_route");
        __builtin_unreachable();
    case route_tier_kind::exact:
        // Refuse to plant an exact entry when a prefix entry
        // for the same canonical path already lives in the radix tier.
        reject_terminus_collision(idx.get_url_complete(),
                                  /*want_is_prefix=*/false);
        exact_routes_.emplace(idx.get_url_complete(),
                              make_non_prefix_entry(methods, std::move(shim)));
        break;
    case route_tier_kind::regex:
        // Regex-tier routes do not conflict with prefix routes because
        // a literal pattern with regex metacharacters is its own key
        // (it never matches as a prefix lookup target).
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re),
             make_non_prefix_entry(methods, std::move(shim))});
        break;
    }
}

void route_table::update_existing_v2_entry(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // The tier was fixed at first registration. For the exact tier a
    // direct map lookup suffices; for the regex tier walk the vector
    // and match by shim identity (regex patterns are not repeated
    // keys; pointer identity is the cheapest and most reliable
    // discriminator).
    //
    // Precondition: `shim` is the shared_ptr that
    // prepare_or_create_lambda_shim extracted from this same regex
    // entry (via find_v2_entry_by_path_) earlier in the SAME
    // route_table_mutex_ unique_lock window, so the entry cannot have
    // been removed or its handler replaced in between. The
    // `rr.entry.handler == shim` identity probe therefore cannot miss:
    // the pointer stored in the entry is the very pointer we hold.
    auto merge_into = [&](route_entry& target) {
        target.methods = target.methods | methods;
        target.handler = shim;
        target.is_prefix = false;
    };
    // Radix-tier updates are handled by upsert_v2_radix_route; only exact
    // and regex entries reach here.
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

void route_table::upsert_v2_table_entry_locked_(
        const http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim, bool fresh) {
    // Caller must already hold route_table_mutex_ (unique_lock). The
    // single-lock window covers the conflict probe (carried out by
    // prepare_or_create_lambda_shim via find_v2_entry_by_path_) all the
    // way through the table mutation here, so no concurrent registration
    // can race in between.
    //
    // We store the lambda_resource shim via the shared_ptr arm so
    // dispatch is identical to class-resource registration. The methods
    // bitmask accumulates across calls when fresh==false. `fresh` is
    // prepare_or_create_lambda_shim's returned bool (the call site in
    // on_methods_ names it `is_new_entry`): true iff the shim was newly
    // constructed because no entry existed at this path.
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        upsert_v2_radix_route(key, methods, std::move(shim));
    } else if (fresh) {
        insert_fresh_v2_entry(idx, methods, std::move(shim));
    } else {
        update_existing_v2_entry(key, methods, std::move(shim));
    }
}

// Helper used by register_v2_route. Throws std::invalid_argument for a
// pre-existing registration at @p idx before any mutation.
namespace {
[[noreturn]] void throw_duplicate_registration(const std::string& key) {
    throw std::invalid_argument(
        "A resource is already registered at this path: '" + key + "'");
}
}  // namespace

void route_table::reject_duplicate_v2_entry_(
        const http_endpoint& idx, bool family) {
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
    case route_tier_kind::regex:
        for (const auto& rr : regex_routes_) {
            if (rr.url_complete == key) throw_duplicate_registration(key);
        }
        break;
    }
}

void route_table::register_v2_route(const http_endpoint& idx,
        std::shared_ptr<http_resource> res, bool family) {
    // Place a register_path / register_prefix registration into the
    // v2 3-tier route table. Tier placement via classify_route_tier()
    // (single source-of-truth):
    //   - family=true  -> segment trie (prefix terminus).
    //   - radix tier   -> segment trie (exact terminus, wildcard nodes).
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
    route_entry entry;
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
    case route_tier_kind::regex:
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re), std::move(entry)});
        break;
    case route_tier_kind::exact:
        exact_routes_.emplace(idx.get_url_complete(), std::move(entry));
        break;
    }
}

}  // namespace detail
}  // namespace httpserver
