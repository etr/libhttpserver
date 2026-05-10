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

// TASK-027: bespoke segment-trie storage for the parameterized + prefix
// route tier. Each node represents one URL path segment; children are
// keyed by the literal segment string, with a single optional wildcard
// child carrying the parameter name (e.g. "{id}" -> wildcard_name_ = "id").
//
// The architecture spec (§4.7) commits only to the OUTER shape (three-tier
// + cache); the radix-tree implementation choice is intentionally left
// open. A segment trie is sufficient for the 9-method / N-segment
// registration shape libhttpserver supports and avoids dragging in a
// vendored library (which would conflict with the project's tightly
// curated source tree and LGPL-2.1 distribution).
//
// Internal header — only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "radix_tree.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_RADIX_TREE_HPP_
#define SRC_HTTPSERVER_DETAIL_RADIX_TREE_HPP_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "httpserver/http_utils.hpp"

namespace httpserver {
namespace detail {

// radix_match: result type of radix_tree<T>::find. `entry` is a non-owning
// pointer into the tree; valid until the next mutation. `captures` lists
// (parameter-name, captured-value) pairs in the order the wildcards
// appear along the matched path. `is_prefix_match` is true iff the match
// came from a `is_prefix=true` registration that did not consume every
// remaining request segment.
template <typename T>
struct radix_match {
    const T* entry = nullptr;
    std::vector<std::pair<std::string, std::string>> captures;
    bool is_prefix_match = false;
};

// Single trie node. Children are split into:
//   - `children_`: keyed by the literal segment string (exact match).
//   - `wildcard_child_`: optional single child consuming any one segment.
//
// Each node may carry an `exact_terminus_` (registration with is_prefix=false
// that ends here) and/or a `prefix_terminus_` (is_prefix=true). The two
// are kept separately because a prefix and an exact registration may both
// terminate at the same node (e.g. /static prefix + /static exact would be
// a user error caught at registration time, but the storage allows it).
template <typename T>
struct radix_node {
    std::unordered_map<std::string, std::unique_ptr<radix_node>> children_;
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

    // Find the most specific match for `path`. Returns true on hit and
    // populates `out`. Lookup preference (most specific first):
    //   1. exact_terminus_ on the matched node, if every request segment
    //      consumed by exact-or-wildcard descent.
    //   2. prefix_terminus_ on the deepest ancestor that has one.
    bool find(std::string_view path, radix_match<T>& out) const {
        out = {};
        const auto segments = tokenize(path);
        const radix_node<T>* node = root_.get();

        // Root path "/" has no segments. Match the root exact terminus
        // first (most specific), falling back to the root prefix terminus.
        if (segments.empty()) {
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

        // Track best prefix candidate seen during descent (deepest wins).
        const T* best_prefix = nullptr;
        std::vector<std::pair<std::string, std::string>> best_prefix_caps;

        // Root prefix terminus: a `register_prefix("/")` matches every
        // request, so seed best_prefix with it before walking deeper.
        if (node->prefix_terminus_.has_value()) {
            best_prefix = &node->prefix_terminus_.value();
            best_prefix_caps.clear();
        }
        std::vector<std::pair<std::string, std::string>> caps;

        for (std::size_t i = 0; i < segments.size(); ++i) {
            const std::string& seg = segments[i];
            // Prefer exact child over wildcard.
            auto it = node->children_.find(seg);
            if (it != node->children_.end()) {
                node = it->second.get();
            } else if (node->wildcard_child_) {
                node = node->wildcard_child_.get();
                caps.emplace_back(node->wildcard_name_, seg);
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
            if (i + 1 == segments.size()
                && node->exact_terminus_.has_value()) {
                out.entry = &node->exact_terminus_.value();
                out.captures = std::move(caps);
                out.is_prefix_match = false;
                return true;
            }
        }

        if (best_prefix != nullptr) {
            out.entry = best_prefix;
            out.captures = std::move(best_prefix_caps);
            out.is_prefix_match = true;
            return true;
        }
        return false;
    }

    // Remove the entry at `path`. is_prefix selects which terminus to
    // clear. Returns true iff a terminus was actually cleared.
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
