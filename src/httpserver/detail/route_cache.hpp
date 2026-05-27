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

// TASK-027: 256-entry LRU cache fronting the 3-tier route table.
//
// Per architecture §4.7 / Constraint 5, lookups consult this cache first
// and on miss walk the tier chain (exact → radix → regex), promoting the
// hit into the cache. The cache key is (method, path) — method is part of
// the key so a path served by both on_get and on_post warms two distinct
// cache entries.
//
// Concurrency: the cache uses its own plain std::mutex (route_cache_mutex_
// in webserver_impl) — *not* a shared_mutex — because every cache touch,
// including the LRU promotion on a hit, is a write (std::list::splice).
// Lock-order discipline: route_table_mutex_ is always acquired before
// route_cache_mutex_ when both are held.
//
// Internal header — only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "route_cache.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_ROUTE_CACHE_HPP_
#define SRC_HTTPSERVER_DETAIL_ROUTE_CACHE_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "httpserver/http_method.hpp"
#include "httpserver/detail/route_entry.hpp"

namespace httpserver {
namespace detail {

// (method, path) cache key. Hashed by combining the path's string hash
// with the method enum value via the boost-style mix-shift.
struct cache_key {
    http_method method = http_method::get;
    std::string path;

    friend bool operator==(const cache_key& a, const cache_key& b) noexcept {
        return a.method == b.method && a.path == b.path;
    }
};

struct cache_key_hash {
    std::size_t operator()(const cache_key& k) const noexcept {
        std::size_t h1 = std::hash<std::string>{}(k.path);
        std::size_t h2 = static_cast<std::size_t>(k.method);
        // boost::hash_combine constant.
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

// cache_value: the hit payload. Carries a copy of the route_entry (one
// shared_ptr ref-bump for the class arm; std::function copy for the
// lambda arm) and the parameter capture vector so the cache hit can
// replay parameter binding without re-walking the radix tree.
struct cache_value {
    route_entry entry;
    // Read in src/webserver.cpp at the cache-hit replay site
    // (`result.captured_params = std::move(cached.captured_params)`); cppcheck
    // analyses each TU in isolation and does not see the cross-TU read.
    // cppcheck-suppress unusedStructMember
    std::vector<std::pair<std::string, std::string>> captured_params;
};

// route_cache: bounded LRU front-end for the tier chain. Bounded to a
// configurable max-size (default 256 per architecture spec). Insertion
// of a new key evicts the back of the LRU list when the size cap is
// reached. find() promotes the hit to the front via splice().
class route_cache {
 public:
    explicit route_cache(std::size_t max_entries = 256)
        : max_entries_(max_entries) {}

    // Find by key; returns true on hit and copies the value into `out`.
    // Promotes the hit to the front of the LRU list as a side effect.
    bool find(const cache_key& key, cache_value& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        list_.splice(list_.begin(), list_, it->second);
        out = it->second->second;
        return true;
    }

    // Zero-allocation warm-path variant: looks up without constructing a
    // cache_key (avoids copying `path` into a std::string on every call,
    // including every warm cache hit). Uses a compatible hash computed
    // from (method, string_view) and a heterogeneous equality check.
    // On hit, copies the value into `out` and promotes the entry.
    // std::hash<std::string_view> produces the same hash as
    // std::hash<std::string> for identical character sequences (C++17
    // standard guarantee), so the probe always lands on the correct
    // bucket.
    bool find_by_view(http_method method, std::string_view path,
                      cache_value& out) {
        // Compute the same hash as cache_key_hash without owning `path`.
        std::size_t h1 = std::hash<std::string_view>{}(path);
        std::size_t h2 = static_cast<std::size_t>(method);
        std::size_t bucket_hash = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL
                                        + (h1 << 6) + (h1 >> 2));
        std::lock_guard<std::mutex> lock(mutex_);
        // Walk the bucket manually via equal_range on the raw bucket index.
        // unordered_map::bucket() + bucket_begin()/bucket_end() lets us
        // scan the correct bucket without constructing a full cache_key.
        std::size_t b = map_.bucket_count() ? bucket_hash % map_.bucket_count() : 0;
        for (auto it = map_.cbegin(b), end = map_.cend(b); it != end; ++it) {
            if (it->first.method == method && it->first.path == path) {
                // Promote: splice requires a mutable iterator from the main map.
                auto main_it = map_.find(it->first);
                list_.splice(list_.begin(), list_, main_it->second);
                out = main_it->second->second;
                return true;
            }
        }
        return false;
    }

    // Insert (or replace) the entry for `key`. Evicts the LRU back if
    // the size cap is reached.
    void insert(const cache_key& key, cache_value value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            // Replace in place; promote.
            it->second->second = std::move(value);
            list_.splice(list_.begin(), list_, it->second);
            return;
        }
        list_.emplace_front(key, std::move(value));
        map_[key] = list_.begin();
        if (map_.size() > max_entries_) {
            auto& back = list_.back();
            map_.erase(back.first);
            list_.pop_back();
        }
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
        list_.clear();
    }

    std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

 private:
    using list_t = std::list<std::pair<cache_key, cache_value>>;

    mutable std::mutex mutex_;
    std::size_t max_entries_;
    list_t list_;
    std::unordered_map<cache_key, typename list_t::iterator,
                       cache_key_hash> map_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_ROUTE_CACHE_HPP_
