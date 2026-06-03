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
#include <regex>  // NOLINT [build/c++11] -- regex is not banned project-wide.
#include <stdexcept>
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
    // wildcard_name_ is the bare parameter name (e.g. "id" for the
    // pattern `{id|([0-9]+)}`), matching v1 semantics: callers reach the
    // captured value via `req.get_arg("id")`, not via the full source
    // token. wildcard_constraint_, when populated, carries the per-segment
    // regex from the `|regex` suffix and is enforced during find(); a
    // segment that fails the constraint is treated as a wildcard miss
    // (the descent breaks, falling back to a prefix candidate or 404).
    std::string wildcard_name_;
    std::optional<std::regex> wildcard_constraint_;
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
    void insert(const std::string& path, T entry, bool is_prefix = false) {
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
            std::string_view seg = pop_next_segment_(rest);
            const radix_node<T>* next = step_to_child_(node, seg, caps);
            if (next == nullptr) break;
            node = next;
            if (node->prefix_terminus_.has_value()) {
                best_prefix = &node->prefix_terminus_.value();
                best_prefix_caps = caps;
            }
            // If we just consumed the last request segment, an exact
            // terminus here beats any prefix candidate.
            if (try_consume_exact_terminus_(rest, node, caps, out)) {
                return true;
            }
        }

        if (best_prefix == nullptr) return false;
        out.entry = best_prefix;
        out.captures = std::move(best_prefix_caps);
        out.is_prefix_match = true;
        return true;
    }

 private:
    // Pop the next '/'-delimited segment off `rest`, advancing rest past it
    // (and past the separator). Extracted from find() so the per-segment
    // logic stays a single statement.
    static std::string_view pop_next_segment_(std::string_view& rest) {
        std::string_view::size_type slash = rest.find('/');
        if (slash == std::string_view::npos) {
            std::string_view seg = rest;
            rest = {};
            return seg;
        }
        std::string_view seg = rest.substr(0, slash);
        rest = rest.substr(slash + 1);
        return seg;
    }

    // If `rest` is fully consumed and `node` carries an exact terminus,
    // populate `out` and return true. Returns false otherwise (caller
    // continues the descent or falls back to the best prefix candidate).
    static bool try_consume_exact_terminus_(std::string_view rest,
            const radix_node<T>* node,
            std::vector<std::pair<std::string, std::string>>& caps,
            radix_match<T>& out) {
        if (!rest.empty() || !node->exact_terminus_.has_value()) return false;
        out.entry = &node->exact_terminus_.value();
        out.captures = std::move(caps);
        out.is_prefix_match = false;
        return true;
    }

    // Take one descent step from `node` along `seg`: prefer an exact child,
    // fall back to a wildcard child (capturing into `caps` and enforcing
    // any per-segment regex constraint). Returns the new node or nullptr
    // when no descent is possible (caller falls back to a prefix candidate
    // or returns 404).
    static const radix_node<T>* step_to_child_(const radix_node<T>* node,
            std::string_view seg,
            std::vector<std::pair<std::string, std::string>>& caps) {
        // Prefer exact child over wildcard. std::map's transparent
        // comparator (std::less<>) accepts std::string_view directly --
        // no temporary std::string is constructed on the hot path.
        auto it = node->children_.find(seg);
        if (it != node->children_.end()) return it->second.get();
        if (!node->wildcard_child_) return nullptr;
        // Per-segment regex constraint enforcement: if the wildcard
        // carries a `|regex` suffix, the actual segment must satisfy
        // it; otherwise the branch is not taken and we fall back to a
        // prefix candidate or 404, matching v1 semantics.
        const radix_node<T>* candidate = node->wildcard_child_.get();
        if (candidate->wildcard_constraint_.has_value()
                && !std::regex_match(seg.begin(), seg.end(),
                                     *candidate->wildcard_constraint_)) {
            return nullptr;
        }
        caps.emplace_back(candidate->wildcard_name_, std::string(seg));
        return candidate;
    }

 public:
    // TASK-056: probe for a terminus of the specified kind at the EXACT
    // node reached by tokenizing `path` (pattern-equality, not request-
    // path matching). Unlike find(), this does NOT fall back to a
    // prefix ancestor; it DOES descend the wildcard child when the
    // pattern segment is wildcard-shaped (same shape rule as remove()),
    // because the caller passes the registered pattern, not a concrete
    // request segment. Returns true iff such a terminus exists.
    //
    // Designed for the registration-time collision guard added in
    // TASK-056 (webserver_impl::reject_terminus_collision): when
    // inserting a NEW exact terminus at /admin we need to refuse if a
    // prefix terminus is already registered at /admin (and vice versa)
    // — silent shadowing would corrupt the (method, path) cache key.
    bool has_terminus_at(const std::string& path, bool is_prefix) const {
        const radix_node<T>* node = walk_registered_pattern_(root_.get(),
                                                             tokenize(path));
        if (node == nullptr) return false;
        return is_prefix ? node->prefix_terminus_.has_value()
                         : node->exact_terminus_.has_value();
    }

    // Remove the entry at `path`. is_prefix selects which terminus to
    // clear. Returns true iff a terminus was actually cleared.
    // NOTE: unlike find(), where descent uses the concrete request-path
    // segment value (e.g. "42"), remove() receives the registered pattern
    // (e.g. "{id}") and matches wildcard nodes by the placeholder shape.
    bool remove(const std::string& path, bool is_prefix) {
        radix_node<T>* node = walk_registered_pattern_(root_.get(),
                                                       tokenize(path));
        if (node == nullptr) return false;
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
    static std::vector<std::string> tokenize(const std::string& path) {
        // tokenize_url takes a const std::string&; passing the already-
        // owned string binds directly, avoiding the std::string{view}
        // temporary that the previous string_view overload required.
        return ::httpserver::http::http_utils::tokenize_url(path);
    }

    // Descend from `start` along `segments`, matching exact children
    // first and falling back to a wildcard child when the segment has
    // the {name} placeholder shape. Returns the terminal node or
    // nullptr if any segment failed to match.
    //
    // Templated on Node (radix_node<T> or const radix_node<T>) so the
    // const-correct mutable / const variants share one descent body --
    // collapses the previous in-place duplicate descent loops in
    // has_terminus_at and remove (PMD CPD finding).
    template <class Node>
    static Node* walk_registered_pattern_(Node* start,
            const std::vector<std::string>& segments) {
        Node* node = start;
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
            return nullptr;
        }
        return node;
    }

    static bool is_wildcard_segment(const std::string& seg) noexcept {
        return seg.size() >= 2 && seg.front() == '{' && seg.back() == '}';
    }

    // Split `{name|regex}` into (bare name, regex-pattern). When no `|`
    // is present, returns (name, ""). The regex pattern (if any) is the
    // substring between the first `|` and the closing `}`. Mirrors
    // http_endpoint::append_parameter_url_part's split rule so the same
    // user-facing `{name|regex}` syntax has identical semantics in both
    // the radix tier and the regex-fallback tier.
    static std::pair<std::string, std::string>
    parse_wildcard(const std::string& seg) {
        // Caller has already verified is_wildcard_segment(seg).
        const std::string::size_type bar = seg.find_first_of('|');
        if (bar == std::string::npos) {
            return {seg.substr(1, seg.size() - 2), {}};
        }
        return {seg.substr(1, bar - 1),
                seg.substr(bar + 1, seg.size() - bar - 2)};
    }

    static radix_node<T>* descend_or_create(radix_node<T>* node,
                                            const std::string& seg) {
        if (is_wildcard_segment(seg)) {
            auto [name, constraint_src] = parse_wildcard(seg);
            if (!node->wildcard_child_) {
                node->wildcard_child_ = std::make_unique<radix_node<T>>();
                node->wildcard_child_->wildcard_name_ = std::move(name);
                if (!constraint_src.empty()) {
                    try {
                        node->wildcard_child_->wildcard_constraint_.emplace(
                            constraint_src,
                            std::regex::extended | std::regex::icase
                                | std::regex::nosubs);
                    } catch (const std::regex_error&) {
                        throw std::invalid_argument(
                            "Not a valid regex in URL: " + constraint_src);
                    }
                }
            }
            // If a wildcard child already exists, the first registered
            // name and constraint win. Re-registering a different name
            // or constraint on the same path is a user error and is
            // caught by the upstream conflict check before insert.
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
