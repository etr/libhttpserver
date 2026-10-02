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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#ifndef SRC_HTTPSERVER_SERVER_ROUTES_HPP_
#define SRC_HTTPSERVER_SERVER_ROUTES_HPP_

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/server/budgets.hpp>

namespace httpserver {

// The per-connection exchange type is defined by the request-handling
// area of the v3 API. Route registration only stores and moves
// handlers, so an incomplete type suffices here by design; the
// definition must live exactly in this namespace.
class exchange;

namespace server {

// TASK-118: one captured path parameter delivered through the exchange
// (plan D2). `name` views the registry's stored pattern text: the view
// stays valid while the registry is not mutated (the native server
// enforces this -- registration is closed once listen() runs); `value`
// is owned.
struct route_captures {
    std::string_view name;
    std::string value;
};

namespace detail {

constexpr bool is_name_byte(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}

// Parameter name: [A-Za-z0-9_]+.
constexpr bool valid_parameter_name(std::string_view name) noexcept {
    if (name.empty()) return false;
    for (const char c : name) {
        if (!is_name_byte(c)) return false;
    }
    return true;
}

// Classifies one slash-delimited pattern segment: a literal token or
// exactly one "{name}". Appends parameter names for duplicate
// detection. Returns a typed failure instead of mutating counters when
// the segment is malformed.
inline http::outcome parse_route_segment(std::string_view segment,
                                         std::vector<std::string_view>& names,
                                         std::size_t& parameters) {
    if (segment.empty()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "route_pattern: empty segment (repeated slash)");
    }
    if (segment.front() != '{') {
        // Literal segment: the RFC 9110 token grammar, which excludes
        // braces, spaces, and separators.
        if (!http::detail::is_token(segment)) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "route_pattern: segment '" + std::string(segment)
                    + "' is not a valid literal token");
        }
        return http::outcome::okay();
    }
    if (segment.size() < 3 || segment.back() != '}') {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "route_pattern: a parameter segment must be exactly one"
            " '{name}'");
    }
    const std::string_view name = segment.substr(1, segment.size() - 2);
    if (!valid_parameter_name(name)) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "route_pattern: parameter name '" + std::string(name)
                + "' must match [A-Za-z0-9_]+");
    }
    for (const std::string_view seen : names) {
        if (seen == name) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "route_pattern: duplicate parameter name '" + std::string(name)
                    + "'");
        }
    }
    names.push_back(name);
    ++parameters;
    return http::outcome::okay();
}

// Splits a path into its slash-delimited segments. The root and the
// empty path have no segments (both denote the same resource).
inline std::vector<std::string_view> split_path_segments(
    std::string_view path) {
    std::vector<std::string_view> segments;
    if (path.size() < 2) return segments;  // "" and "/"
    std::size_t pos = 1;  // skip the leading slash
    while (pos < path.size()) {
        const std::size_t slash = path.find('/', pos);
        const std::size_t width =
            slash == std::string_view::npos ? std::string_view::npos
                                            : slash - pos;
        segments.push_back(path.substr(pos, width));
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    return segments;
}

// Compares one pattern segment run against the path: a literal
// pattern segment compares equal, a single {name} segment captures any
// non-empty segment. Captures fill in pattern order, each name viewing
// the pattern text; a failed match leaves them unused.
inline bool match_segment_run(
    const std::vector<std::string_view>& pattern_segments,
    const std::vector<std::string_view>& path_segments,
    std::vector<route_captures>& captures) {
    captures.clear();
    for (std::size_t i = 0; i < pattern_segments.size(); ++i) {
        const std::string_view& pattern = pattern_segments[i];
        const std::string_view& segment = path_segments[i];
        if (pattern.front() == '{') {
            if (segment.empty()) return false;
            captures.push_back(route_captures{
                pattern.substr(1, pattern.size() - 2), std::string(segment)});
            continue;
        }
        if (pattern != segment) return false;
    }
    return true;
}

// Segment-wise full pattern match over an already-split path: the
// segment counts must agree, then the whole run matches.
inline bool match_path_segments(
    std::string_view pattern_text,
    const std::vector<std::string_view>& path_segments,
    std::vector<route_captures>& captures) {
    const std::vector<std::string_view> pattern_segments =
        split_path_segments(pattern_text);
    if (pattern_segments.size() != path_segments.size()) return false;
    return match_segment_run(pattern_segments, path_segments, captures);
}

// Prefix-family match (TASK-118, plan D1): the pattern segments match
// a leading run of the path segments, equal length included. The root
// pattern "/" has no segments and therefore matches every path (the
// documented catch-all).
inline bool match_prefix_segments(
    std::string_view pattern_text,
    const std::vector<std::string_view>& path_segments,
    std::vector<route_captures>& captures) {
    const std::vector<std::string_view> pattern_segments =
        split_path_segments(pattern_text);
    if (pattern_segments.size() > path_segments.size()) return false;
    return match_segment_run(pattern_segments, path_segments, captures);
}

}  // namespace detail

// A validated route pattern: "/" or a slash-separated sequence of
// literal-token segments, each of which may instead be exactly one
// "{name}" parameter whose name matches [A-Za-z0-9_] and is unique
// within the pattern. The stored text is canonical: one leading slash,
// no trailing slash, no empty segments. parse() reports invalid_argument
// and leaves `out` untouched on failure.
class route_pattern {
 public:
    route_pattern() noexcept = default;

    static http::outcome parse(std::string_view text, route_pattern& out) {
        if (text.empty()) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "route_pattern: pattern is empty");
        }
        if (text.front() != '/') {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "route_pattern: pattern must begin with '/'");
        }
        route_pattern parsed;
        if (text != "/") {
            if (text.back() == '/') {
                return http::outcome(
                    http::outcome_code::invalid_argument,
                    "route_pattern: pattern must not end with '/'");
            }
            std::vector<std::string_view> names;
            std::size_t pos = 1;
            while (pos <= text.size()) {
                const std::size_t slash = text.find('/', pos);
                const std::size_t width =
                    slash == std::string_view::npos ? std::string_view::npos
                                                    : slash - pos;
                if (const http::outcome result = detail::parse_route_segment(
                        text.substr(pos, width), names, parsed.parameters_);
                    !result.ok()) {
                    return result;
                }
                ++parsed.segments_;
                if (slash == std::string_view::npos) break;
                pos = slash + 1;
            }
        }
        parsed.text_ = std::string(text);
        out = std::move(parsed);
        return http::outcome::okay();
    }

    // Canonical pattern text; empty for a default-constructed pattern.
    const std::string& text() const noexcept { return text_; }

    std::size_t segment_count() const noexcept { return segments_; }

    std::size_t parameter_count() const noexcept { return parameters_; }

 private:
    std::string text_;
    std::size_t segments_ = 0;
    std::size_t parameters_ = 0;
};

// The canonical coroutine handler of the v3 API: a lazily started task
// over the per-connection exchange (architecture §3.1, DR-V3-003).
using route_handler = concurrency::unique_function<task<void>(exchange&)>;

// The value one synchronous route returns: terminal status, response
// fields, and the complete body bytes. A plain aggregate; an invalid
// status makes the route's runner synthesize the 500.
struct sync_response {
    http::status status;  // invalid -> the runner's synthesized 500
    http::fields fields;  // handler-pinned framing passes through
    std::vector<std::byte> body;
};

// The value-returning handler (DR-V3-003): receives the complete
// request head and the bounded buffered body (empty for a bodyless
// request) and returns the response value. No coroutine code.
using sync_route_handler = concurrency::unique_function<sync_response(const http::request_head&, std::span<const std::byte>)>;

// Budget-bounded registration table: the seed of the immutable
// published route generation. A registration is visible to every
// enabled HTTP version by construction (PRD-V3N-REQ-009): neither the
// API nor a stored entry carries a protocol dimension. Each admitted
// route holds one reservation against the routes resource of its
// budget node and every ancestor, so nested scopes share one limit.
class route_registry {
 public:
    route_registry() noexcept = default;

    // Attaches a registry to a budget scope. invalid_argument when the
    // budget carries no routes capacity (an empty budget or a zero
    // capacity); `out` is untouched on failure.
    static http::outcome create(const resource_budget& budget,
                                route_registry& out) {
        if (budget.capacity(resource::routes) == 0) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "route_registry: the budget carries no routes capacity");
        }
        out = route_registry(budget);
        return http::outcome::okay();
    }

    // Registers `handler` for (method, pattern) -- the single-method
    // form; an extension method registers exactly itself. Typed
    // failures: invalid_argument for the unknown_ method, a malformed
    // pattern, or an empty handler; invalid_state for a duplicate
    // (overlapping methods, same pattern, same family) or a registry
    // with no budget; limit_exceeded when the routes capacity is
    // exhausted on this node or an ancestor. On failure the registry is
    // unchanged.
    http::outcome route(const http::method& m, std::string_view pattern,
                        route_handler handler) {
        if (!m.valid()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "route_registry: the unknown_ method is not registrable");
        }
        if (!handler) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "route_registry: handler is empty");
        }
        http::method_set bits;
        http::method single;
        if (m.is_extension()) {
            single = m;
        } else {
            bits.set(m.id());
        }
        return admit(pattern, std::move(handler), false, bits, single);
    }

    // Registers `handler` for a set of known methods on one pattern.
    // An empty set is invalid_argument; the duplicate rule is
    // overlapping-methods on the same pattern (disjoint sets coexist
    // and the 405 Allow merges them).
    http::outcome route(const http::method_set& set,
                        std::string_view pattern, route_handler handler) {
        if (!set.any()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "route_registry: a method-set route needs at least one"
                " method");
        }
        if (!handler) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "route_registry: handler is empty");
        }
        return admit(pattern, std::move(handler), false, set,
                     http::method{});
    }

    // Registers `handler` as a prefix-family route: the pattern
    // segments match a leading run of the request path segments (equal
    // length included; "/" is the catch-all). Admission rules match
    // route(); full-match routes outrank prefix routes at resolve time.
    http::outcome route_prefix(const http::method_set& set,
                               std::string_view pattern,
                               route_handler handler) {
        if (!set.any()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "route_registry: a prefix route needs at least one"
                " method");
        }
        if (!handler) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "route_registry: handler is empty");
        }
        return admit(pattern, std::move(handler), true, set,
                     http::method{});
    }

    // Exact-segment visibility probe over the stored patterns (no
    // capture evaluation); the request matcher arrives with the
    // request-handling tasks.
    bool registered(const http::method& m, const route_pattern& p) const {
        for (const entry& known : entries_) {
            if (known.pattern_.text() == p.text() && accepts(known, m)) {
                return true;
            }
        }
        return false;
    }

    // One lookup outcome: the registered handler (null on a miss) and
    // the parameter captures in pattern order.
    struct match_result {
        const route_handler* handler = nullptr;
        std::vector<std::string> parameters;
    };

    // Resolves a request head to its route. Segment-wise: literal
    // segments compare equal, a {name} segment captures any non-empty
    // segment, first match in registration order wins, a miss yields a
    // null handler. Const and lock-free. No protocol dimension (REQ-009):
    // one registration serves every enabled version.
    match_result match(const http::method& m,
                       std::string_view route_path) const {
        match_result found;
        const std::vector<std::string_view> path_segments =
            detail::split_path_segments(route_path);
        for (const entry& known : entries_) {
            if (known.prefix_ || !accepts(known, m)) continue;
            std::vector<route_captures> captures;
            if (!detail::match_path_segments(known.pattern_.text(),
                                             path_segments, captures)) {
                continue;
            }
            found.handler = &known.handler_;
            found.parameters.reserve(captures.size());
            for (route_captures& capture : captures) {
                found.parameters.push_back(std::move(capture.value));
            }
            break;
        }
        return found;
    }

    // TASK-118: the request-facing lookup with the v2 family
    // precedence (plan D1). Full matches (exact and parameterized,
    // first registration order) are decided before prefix matches
    // (most segments wins, ties by registration order); the tier
    // decision is method-blind, exactly like the v2 lookup tiers, so a
    // full-match pattern that mismatches the method owns the 405. A
    // hit reports the handler, the named captures in pattern order,
    // and the matched pattern; a method miss reports the tier's merged
    // methods (the Allow inputs: the known-slot set plus any extension
    // singles' tokens); a plain miss reports neither. The v2 regex
    // family and {name|regex} per-segment constraints are not ported
    // (migration note in the v2-parity inventory).
    struct resolve_result {
        enum class resolve_kind : std::uint8_t { miss, method_miss, hit };

        resolve_kind kind = resolve_kind::miss;
        const route_handler* handler = nullptr;
        std::vector<route_captures> captures;
        std::string pattern_text;
        http::method_set methods;
        std::vector<std::string> extension_names;
        bool is_prefix = false;
    };

    resolve_result resolve(const http::method& m,
                           std::string_view route_path) const {
        const std::vector<std::string_view> path_segments =
            detail::split_path_segments(route_path);
        resolve_result full = resolve_tier(m, path_segments, false);
        if (full.kind != resolve_result::resolve_kind::miss) return full;
        return resolve_tier(m, path_segments, true);
    }

    std::size_t size() const noexcept { return entries_.size(); }

    const resource_budget& budget() const noexcept { return budget_; }

 private:
    explicit route_registry(resource_budget budget) noexcept
        : budget_(std::move(budget)) { }

    struct entry {
        http::method_set methods_;              // known-slot methods
        http::method single_;                   // extension registration
        route_pattern pattern_;
        route_handler handler_;
        reservation seat_;
        bool prefix_ = false;
    };

    // True iff the entry would serve @p m: a known method tests the
    // slot set; an extension single compares by identity.
    static bool accepts(const entry& known, const http::method& m) noexcept {
        if (known.single_.valid()) return known.single_ == m;
        return known.methods_.contains(m);
    }

    // The shared admission tail of every registration form: parse,
    // budget gate, same-family duplicate check (overlapping methods on
    // one canonical pattern), then the reserved push.
    http::outcome admit(std::string_view pattern, route_handler handler,
                        bool prefix, const http::method_set& set,
                        const http::method& single) {
        route_pattern parsed;
        if (const http::outcome result =
                route_pattern::parse(pattern, parsed);
            !result.ok()) {
            return result;
        }
        if (!budget_.valid()) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "route_registry: registry has no budget");
        }
        const std::string& canonical = parsed.text();
        for (const entry& known : entries_) {
            if (known.prefix_ == prefix && known.pattern_.text() == canonical
                    && overlaps(known, set, single)) {
                const std::string label = single.valid()
                    ? std::string(single.name()) : http::to_string(set);
                return http::outcome(
                    http::outcome_code::invalid_state,
                    "route_registry: duplicate route for " + label + " "
                        + canonical);
            }
        }
        reservation seat;
        if (const http::outcome result =
                budget_.reserve(resource::routes, 1, seat);
            !result.ok()) {
            return result;
        }
        entries_.push_back(entry{set, single, std::move(parsed),
                                 std::move(handler), std::move(seat),
                                 prefix});
        return http::outcome::okay();
    }

    // Overlap test for the duplicate rule: two known-slot sets
    // intersect, or two extension singles name the same token. An
    // extension single and a known-slot set are always disjoint.
    static bool overlaps(const entry& known, const http::method_set& set,
                         const http::method& single) noexcept {
        if (single.valid()) {
            return known.single_.valid() && known.single_ == single;
        }
        for (std::size_t id = 0;
             id < static_cast<std::size_t>(http::method_id::extension);
             ++id) {
            const http::method slot =
                http::method::known(static_cast<http::method_id>(id));
            if (set.contains(slot) && known.methods_.contains(slot)) {
                return true;
            }
        }
        return false;
    }

    // One tier of the resolve decision. Merges every matching entry's
    // methods (the Allow inputs); the first accepting full match wins,
    // the deepest accepting prefix match wins.
    resolve_result resolve_tier(
            const http::method& m,
            const std::vector<std::string_view>& path_segments,
            bool prefix) const {
        resolve_result out;
        const entry* best = nullptr;
        std::vector<route_captures> best_captures;
        for (const entry& known : entries_) {
            if (known.prefix_ != prefix) continue;
            std::vector<route_captures> captures;
            if (!match_tier_entry(known, path_segments, prefix, captures)) {
                continue;
            }
            merge_entry_methods(out, known);
            if (!accepts(known, m) || !outranks(best, known, prefix)) {
                continue;
            }
            best = &known;
            best_captures = std::move(captures);
        }
        if (best == nullptr) return miss_or_method_miss(out);
        out.kind = resolve_result::resolve_kind::hit;
        out.handler = &best->handler_;
        out.captures = std::move(best_captures);
        out.pattern_text = best->pattern_.text();
        out.is_prefix = prefix;
        return out;
    }

    // Path-shape match of one entry within its tier.
    static bool match_tier_entry(
            const entry& known,
            const std::vector<std::string_view>& path_segments, bool prefix,
            std::vector<route_captures>& captures) {
        const std::string& text = known.pattern_.text();
        return prefix ? detail::match_prefix_segments(text, path_segments,
                                                      captures)
                      : detail::match_path_segments(text, path_segments,
                                                    captures);
    }

    // Folds one matching entry's methods into the tier's Allow inputs.
    static void merge_entry_methods(resolve_result& out,
                                    const entry& known) noexcept {
        if (known.single_.valid()) {
            out.extension_names.emplace_back(known.single_.name());
        } else {
            out.methods.merge(known.methods_);
        }
    }

    // The candidate-selection rule: no winner yet always takes; the
    // full tier keeps its first winner; the prefix tier takes only a
    // strictly deeper pattern.
    static bool outranks(const entry* best, const entry& candidate,
                         bool prefix) noexcept {
        if (best == nullptr) return true;
        if (!prefix) return false;
        return candidate.pattern_.segment_count()
            > best->pattern_.segment_count();
    }

    // No accepting entry: a miss unless the tier matched some pattern
    // shape (then the merged methods feed the 405 Allow).
    static resolve_result miss_or_method_miss(resolve_result out) noexcept {
        if (out.methods.any() || !out.extension_names.empty()) {
            out.kind = resolve_result::resolve_kind::method_miss;
        }
        return out;
    }

    std::vector<entry> entries_;
    resource_budget budget_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_ROUTES_HPP_
