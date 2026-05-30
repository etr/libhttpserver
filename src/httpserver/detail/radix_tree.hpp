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

// TASK-027: segment-trie for the parameterized + prefix route tier.
//
// A segment trie avoids dragging in a vendored library (which would
// conflict with the project's tightly curated source tree and
// LGPL-2.1 distribution). The architecture spec (§4.7) commits only
// to the outer three-tier + cache shape; the implementation choice
// is intentionally left open.
//
// Internal header — only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "radix_tree.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_RADIX_TREE_HPP_
#define SRC_HTTPSERVER_DETAIL_RADIX_TREE_HPP_

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/http_utils.hpp"

namespace httpserver {
namespace detail {

// radix_match: result type of radix_tree<T>::find.
// `entry` is a non-owning pointer into the tree; valid until the next mutation.
template <typename T>
struct radix_match {
    const T* entry = nullptr;
    std::vector<std::pair<std::string, std::string>> captures;
    bool is_prefix_match = false;
};

// Single trie node. `wildcard_child_` is a single optional pointer (not
// another map entry) because there can be at most one unnamed wildcard per
// path level by design — two {name} siblings at the same depth are
// ambiguous and rejected at registration time.
template <typename T>
struct radix_node {
    // TASK-056: per-segment children are kept in std::map rather than
    // std::unordered_map for hash-flooding immunity (CWE-407). URL path
    // segments are attacker-controlled and neither libc++ nor libstdc++
    // seed std::hash<std::string> by default, so std::unordered_map is
    // vulnerable to algorithmic-complexity DoS via crafted sibling keys.
    // std::map (red-black tree) gives O(log n) worst-case per probe with
    // no hashing in the loop. Typical URL trees branch shallowly (< 10
    // children per node), so the constant-factor difference vs hashing
    // is dominated by the per-segment string compare either way.
    //
    // std::less<> is the transparent comparator: it lets find() take a
    // std::string_view directly and compare against the std::string keys
    // without constructing a temporary std::string per probe.
    std::map<std::string, std::unique_ptr<radix_node>, std::less<>> children_;
    std::unique_ptr<radix_node> wildcard_child_;
    std::string wildcard_name_;
    std::optional<T> exact_terminus_;
    std::optional<T> prefix_terminus_;
};

// radix_tree<T>: segment-trie. Inserts route paths split on '/', supports
// `{name}` wildcard segments, and carries a `is_prefix` flag per insertion
// so the same tree backs both parameterized exact and prefix registrations.
//
// Concurrency: this type is NOT internally synchronized. The owning
// webserver_impl protects all three tier structures (exact_routes_,
// param_and_prefix_routes_, regex_routes_) with a single std::shared_mutex.
template <typename T>
class radix_tree {
 public:
    radix_tree() : root_(std::make_unique<radix_node<T>>()) {}

    // Insert `path` with the given entry. is_prefix selects whether the
    // entry terminates in `prefix_terminus_` (and matches any deeper
    // request path) or `exact_terminus_` (and matches only this path).
    // The radix_tree itself does not look inside `entry` — the caller
    // (webserver_impl) is responsible for keeping the is_prefix argument
    // consistent with route_entry::is_prefix, which is the §4.7 source
    // of truth. Replaces an existing terminus of the same kind.
    void insert(std::string_view path, T entry, bool is_prefix = false) {
        radix_node<T>* node = root_.get();
        const auto segments = tokenize(path);
        for (const std::string& seg : segments) {
            node = descend_or_create(node, seg);
        }
        if (is_prefix) {
            node->prefix_terminus_ = std::move(entry);
        } else {
            node->exact_terminus_ = std::move(entry);
        }
    }

    // Match the root node's termini (exact first, then prefix). Pulled
    // out of find() so the empty-segments branch stays a one-liner.
    bool match_root_terminus(radix_match<T>& out) const {
        const radix_node<T>* node = root_.get();
        if (node->exact_terminus_.has_value()) {
            out.entry = &node->exact_terminus_.value();
            out.is_prefix_match = false;
            return true;
        }
        if (node->prefix_terminus_.has_value()) {
            out.entry = &node->prefix_terminus_.value();
            out.is_prefix_match = true;
            return true;
        }
        return false;
    }

    // Find the most specific match for `path`. Returns true on hit and
    // populates `out`. Lookup preference (most specific first):
    //   1. exact_terminus_ on the matched node, if every request segment
    //      consumed by exact-or-wildcard descent.
    //   2. prefix_terminus_ on the deepest ancestor that has one.
    //
    // Hot-path implementation: iterates `path` directly without calling
    // tokenize(), avoiding the std::vector<std::string> allocation and
    // per-segment string copies. Each segment is extracted as a
    // std::string_view and compared against children_ keys (std::string)
    // via the transparent comparator (std::less<>) on std::map — no
    // temporary std::string is constructed per probe.
    // The wildcard path copies the segment into captures<string,string>
    // only when a wildcard node is taken.
    bool find(std::string_view path, radix_match<T>& out) const {
        out = {};
        const radix_node<T>* node = root_.get();

        // Strip a single leading slash; a bare "/" normalises to empty.
        std::string_view rest = path;
        if (!rest.empty() && rest.front() == '/') rest.remove_prefix(1);

        if (rest.empty()) return match_root_terminus(out);

        // Track best prefix candidate seen during descent (deepest wins).
        // Root prefix terminus: a `register_prefix("/")` matches every
        // request, so seed best_prefix with it before walking deeper.
        const T* best_prefix = node->prefix_terminus_.has_value()
            ? &node->prefix_terminus_.value() : nullptr;
        std::vector<std::pair<std::string, std::string>> best_prefix_caps;
        std::vector<std::pair<std::string, std::string>> caps;

        while (!rest.empty()) {
            // Extract the next segment up to the next '/' (or end).
            std::string_view seg;
            std::string_view::size_type slash = rest.find('/');
            if (slash == std::string_view::npos) {
                seg = rest;
                rest = {};
            } else {
                seg = rest.substr(0, slash);
                rest = rest.substr(slash + 1);
            }

            // Prefer exact child over wildcard. std::map's transparent
            // comparator (std::less<>) accepts std::string_view directly
            // — no temporary std::string is constructed on the hot path.
            auto it = node->children_.find(seg);
            if (it != node->children_.end()) {
                node = it->second.get();
            } else if (node->wildcard_child_) {
                node = node->wildcard_child_.get();
                caps.emplace_back(node->wildcard_name_, std::string(seg));
            } else {
                // No way forward: best we can do is the deepest prefix
                // candidate seen (or nothing).
                break;
            }
            if (node->prefix_terminus_.has_value()) {
                best_prefix = &node->prefix_terminus_.value();
                best_prefix_caps = caps;
            }
            // If we just consumed the last request segment AND this node
            // carries an exact terminus, that beats any prefix candidate.
            if (rest.empty() && node->exact_terminus_.has_value()) {
                out.entry = &node->exact_terminus_.value();
                out.captures = std::move(caps);
                out.is_prefix_match = false;
                return true;
            }
        }

        if (best_prefix == nullptr) return false;
        out.entry = best_prefix;
        out.captures = std::move(best_prefix_caps);
        out.is_prefix_match = true;
        return true;
    }

    // TASK-056: probe for a terminus of the specified kind at the EXACT
    // node reached by tokenizing `path` (pattern-equality, not request-
    // path matching). Unlike find(), this does NOT fall back to a
    // prefix ancestor and does NOT descend the wildcard branch — the
    // caller is asking "is there a `{name}` literal segment registered
    // here?", so the same wildcard-shape matching rule as remove() is
    // used. Returns true iff such a terminus exists.
    //
    // Designed for the registration-time collision guard added in
    // TASK-056 (webserver_impl::reject_terminus_collision): when
    // inserting a NEW exact terminus at /admin we need to refuse if a
    // prefix terminus is already registered at /admin (and vice versa)
    // — silent shadowing would corrupt the (method, path) cache key.
    bool has_terminus_at(std::string_view path, bool is_prefix) const {
        const radix_node<T>* node = root_.get();
        const auto segments = tokenize(path);
        for (const std::string& seg : segments) {
            auto it = node->children_.find(seg);
            if (it != node->children_.end()) {
                node = it->second.get();
                continue;
            }
            if (node->wildcard_child_ && is_wildcard_segment(seg)) {
                node = node->wildcard_child_.get();
                continue;
            }
            return false;
        }
        return is_prefix ? node->prefix_terminus_.has_value()
                         : node->exact_terminus_.has_value();
    }

    // Remove the entry at `path`. is_prefix selects which terminus to
    // clear. Returns true iff a terminus was actually cleared.
    // NOTE: unlike find(), where descent uses the concrete request-path
    // segment value (e.g. "42"), remove() receives the registered pattern
    // (e.g. "{id}") and matches wildcard nodes by the placeholder shape.
    bool remove(std::string_view path, bool is_prefix) {
        radix_node<T>* node = root_.get();
        const auto segments = tokenize(path);
        for (const std::string& seg : segments) {
            auto it = node->children_.find(seg);
            if (it != node->children_.end()) {
                node = it->second.get();
                continue;
            }
            // Walk wildcard child if seg matches the {name} placeholder
            // shape. We compare the exact registered key, so removal of
            // /users/{id} requires the same {id} string.
            if (node->wildcard_child_ && is_wildcard_segment(seg)) {
                node = node->wildcard_child_.get();
                continue;
            }
            return false;
        }
        if (is_prefix) {
            if (!node->prefix_terminus_.has_value()) return false;
            node->prefix_terminus_.reset();
        } else {
            if (!node->exact_terminus_.has_value()) return false;
            node->exact_terminus_.reset();
        }
        return true;
        // Note: we do not collapse empty branches. This is intentional —
        // dead nodes are cheap (a few pointers) and avoiding rebalancing
        // keeps the data structure trivially safe under the writer lock.
    }

    bool empty() const noexcept {
        return is_node_empty(root_.get());
    }

 private:
    static std::vector<std::string> tokenize(std::string_view path) {
        // tokenize_url takes a std::string by value via string_split; copy
        // the view's contents to call it.
        return ::httpserver::http::http_utils::tokenize_url(std::string{path});
    }

    static bool is_wildcard_segment(const std::string& seg) noexcept {
        return seg.size() >= 2 && seg.front() == '{' && seg.back() == '}';
    }

    static radix_node<T>* descend_or_create(radix_node<T>* node,
                                            const std::string& seg) {
        if (is_wildcard_segment(seg)) {
            // Strip the braces: "{id}" -> "id".
            std::string name = seg.substr(1, seg.size() - 2);
            if (!node->wildcard_child_) {
                node->wildcard_child_ = std::make_unique<radix_node<T>>();
                node->wildcard_child_->wildcard_name_ = std::move(name);
            }
            // If a wildcard child already exists with a different name,
            // we keep the first registered name. Re-registering with a
            // different name on the same path is a user error and would
            // be caught by the upstream conflict check before insert.
            return node->wildcard_child_.get();
        }
        auto it = node->children_.find(seg);
        if (it == node->children_.end()) {
            it = node->children_.emplace(seg,
                std::make_unique<radix_node<T>>()).first;
        }
        return it->second.get();
    }

    static bool is_node_empty(const radix_node<T>* n) noexcept {
        if (n == nullptr) return true;
        if (n->exact_terminus_.has_value()
            || n->prefix_terminus_.has_value()) return false;
        for (const auto& kv : n->children_) {
            if (!is_node_empty(kv.second.get())) return false;
        }
        if (n->wildcard_child_
            && !is_node_empty(n->wildcard_child_.get())) return false;
        return true;
    }

    std::unique_ptr<radix_node<T>> root_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_RADIX_TREE_HPP_
