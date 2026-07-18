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

// v2 3-tier route table + LRU cache. Internal header; only reachable
// when compiling libhttpserver translation units. NOT part of the
// installed surface; consumers cannot reach it through the public
// umbrella.
#if !defined(HTTPSERVER_COMPILATION)
#error "route_table.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_ROUTE_TABLE_HPP_
#define SRC_HTTPSERVER_DETAIL_ROUTE_TABLE_HPP_

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "httpserver/http_method.hpp"
#include "httpserver/detail/route_cache.hpp"
#include "httpserver/detail/route_entry.hpp"
#include "httpserver/detail/segment_trie.hpp"

namespace httpserver {

class http_resource;

namespace detail {

class http_endpoint;

// route_table -- the v2 routing surface: a 3-tier route table
// (exact_routes_ / param_and_prefix_routes_ / regex_routes_) fronted by
// an LRU cache (route_lru_cache). Owns the single route_table_mutex_
// that guards the three tiers; the LRU cache carries its own internal
// mutex.
//
// **Lock order.** route_table_mutex_ is acquired BEFORE the cache's
// internal mutex when both are conceptually in play. The lookup pipeline
// never holds both at once: it takes a brief shared_lock on the table to
// walk the tiers, releases it, then promotes/inserts into the LRU cache.
// Registration takes a unique_lock on the table, releases it, then clears
// the cache (invalidate_route_cache). This table-before-cache ordering is
// an internal invariant of this class.
//
// **CWE-407 hash-flooding immunity.** exact_routes_ uses std::map (not
// std::unordered_map) so the keyed lookup on the dispatch hot path is
// hash-free. std::less<> enables transparent string_view lookup without
// constructing a temporary std::string.
//
// **Lock-ownership split (see project decomposition memo).** The on_*/
// route registration sequence is orchestrated from webserver_impl (it
// creates the lambda_resource shim -- registration POLICY that does not
// belong in the table). This class therefore exposes lock_for_write() so
// the orchestrator can hold route_table_mutex_ across the whole probe ->
// shim-create -> commit -> upsert sequence, and the "locked" primitives
// (find_v2_entry_by_path_, upsert_v2_table_entry_locked_,
// reject_duplicate_v2_entry_) assume the caller already holds that write
// lock. lookup_v2 / invalidate_route_cache / register_v2_route lock
// internally.
//
// Members are public: the class is an internal implementation detail and
// several white-box tests + the unregister sweeps in webserver_register.cpp
// poke the tiers directly under lock_for_write(). Same posture as
// webserver_impl and hook_bus.
class route_table {
 public:
    // LRU cache size: 256 entries.
    static constexpr std::size_t ROUTE_CACHE_MAX_SIZE = 256;

    // tier_hit identifies which tier answered a lookup. Returned
    // alongside the route_entry copy from lookup_v2() so the dispatch
    // site (and tests) can pin the lookup pipeline.
    enum class tier_hit {
        none,
        cache,
        exact,
        radix,
        regex
    };

    struct lookup_result {
        bool found = false;
        tier_hit tier = tier_hit::none;
        route_entry entry{};
        std::vector<std::pair<std::string, std::string>> captured_params;
    };

    // Pre-compiled regex objects: a (compiled std::regex, route_entry)
    // pair so that lookup_v2 calls std::regex_match on an already-compiled
    // object without paying the compilation cost on every cache miss.
    // url_complete is stored alongside the compiled regex to support O(n)
    // removal (unregister sweeps) without a second map.
    struct regex_route {
        std::string url_complete;
        std::regex compiled_re;
        route_entry entry;
    };

    route_table() = default;
    route_table(const route_table&) = delete;
    route_table& operator=(const route_table&) = delete;
    route_table(route_table&&) = delete;
    route_table& operator=(route_table&&) = delete;
    ~route_table() = default;

    // Walk the v2 route table for (method, path) per the lookup pipeline.
    // Returns lookup_result; populates `tier` even on miss
    // (tier_hit::none) so callers can branch deterministically. Locks the
    // table (shared) internally; never holds the table and cache locks at
    // once.
    lookup_result lookup_v2(http_method method, const std::string& path);

    // Clear the LRU cache. Called by registration paths AFTER the table
    // lock is released (route_cache::clear takes the cache's own mutex).
    // Caller must NOT hold route_table_mutex_ here.
    void invalidate_route_cache();

    // Scoped write-lock accessor. The on_*/route + register/unregister
    // orchestration (webserver_routes.cpp / webserver_register.cpp) holds
    // this across the conflict probe and the table mutation so the two are
    // atomic against concurrent registration/dispatch.
    std::unique_lock<std::shared_mutex> lock_for_write() {
        return std::unique_lock<std::shared_mutex>(route_table_mutex_);
    }

    // Returns the route_entry that maps to @p idx, or nullptr if none
    // exists. Probes the three tiers in lookup order (exact -> radix ->
    // regex) but matches on the canonical registration key
    // (idx.get_url_complete()) rather than a request URL: the on_*/route
    // conflict oracle asks "is there already an entry registered AT this
    // path". Caller must hold route_table_mutex_ (any lock kind); the
    // returned pointer is valid only while that lock is held.
    const route_entry* find_v2_entry_by_path_(
        const http_endpoint& idx) const noexcept;

    // Merge/insert an on_*/route shim into the tier the endpoint's shape
    // selects. Caller must hold route_table_mutex_ (unique_lock) across
    // the whole prepare -> commit -> upsert sequence. `fresh` is true iff
    // no entry previously existed at this path.
    void upsert_v2_table_entry_locked_(const http_endpoint& idx,
                                       method_set methods,
                                       std::shared_ptr<http_resource> shim,
                                       bool fresh);

    // Throw std::invalid_argument if a same-kind v2 entry already exists
    // at the canonical key. Caller must hold route_table_mutex_
    // (unique_lock); the probe runs before any mutation so the atomicity
    // contract holds.
    void reject_duplicate_v2_entry_(const http_endpoint& idx, bool family);

    // Place a register_path / register_prefix registration into the
    // 3-tier table. Takes route_table_mutex_ (unique_lock) internally and
    // runs the collision + duplicate probes BEFORE any mutation, so a
    // rejected registration leaves the table in its prior state.
    void register_v2_route(const http_endpoint& idx,
                           std::shared_ptr<http_resource> res,
                           bool family);

    // --- Route-table state -----------------------------------------------
    // One shared_mutex covering the three tiers. Writes (registration)
    // are rare; reads (dispatch lookups) take shared ownership briefly.
    std::shared_mutex route_table_mutex_;
    std::map<std::string, route_entry, std::less<>> exact_routes_;
    segment_trie<route_entry> param_and_prefix_routes_;
    std::vector<regex_route> regex_routes_;

    // LRU front-end for the route table.
    route_cache route_lru_cache{ROUTE_CACHE_MAX_SIZE};

 private:
    // Locked upsert sub-helpers (caller holds route_table_mutex_).
    void upsert_v2_radix_route(const std::string& key,
                               method_set methods,
                               std::shared_ptr<http_resource> shim);
    void reject_terminus_collision(const std::string& key, bool want_is_prefix);
    void insert_fresh_v2_entry(const http_endpoint& idx,
                               method_set methods,
                               std::shared_ptr<http_resource> shim);
    void update_existing_v2_entry(const std::string& key,
                                  method_set methods,
                                  std::shared_ptr<http_resource> shim);
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_ROUTE_TABLE_HPP_
