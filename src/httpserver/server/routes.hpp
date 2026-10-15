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
#include <cstdint>
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
http::outcome parse_route_segment(std::string_view segment,
                                         std::vector<std::string_view>& names,
                                         std::size_t& parameters);

// Splits a path into its slash-delimited segments. The root and the
// empty path have no segments (both denote the same resource).
std::vector<std::string_view> split_path_segments(
    std::string_view path);

// One admission-time pattern segment: byte offsets into the stored
// pattern text rather than views, so the stored form stays valid
// across entry moves (vector reallocation) for the registry's whole
// lifetime.
struct pattern_segment {
    std::uint32_t offset;
    std::uint32_t length;
};

// Splits a canonical pattern into offset/length segments ONCE, at
// admission (registration time). Patterns are immutable once
// registered and registration closes at listen(), so resolve/match
// consume this stored form and never re-split a pattern per request.
std::vector<pattern_segment> index_path_segments(
    std::string_view path);

// Matches the stored (admission-time) pattern segments against the
// request's already-split path: a literal segment compares equal, a
// single {name} segment captures any non-empty segment (the capture
// name views the pattern text). The prefix family matches a leading
// run of the path segments (equal length included; "/" therefore
// matches every path); the full family requires equal counts.
// Allocation-free beyond the captures the accepted run fills.
bool match_stored_segments(
    std::string_view pattern_text,
    const std::vector<pattern_segment>& pattern_segments, bool prefix,
    const std::vector<std::string_view>& path_segments,
    std::vector<route_captures>& captures);

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

    static http::outcome parse(std::string_view text, route_pattern& out);

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
                                route_registry& out);

    // Registers `handler` for (method, pattern) -- the single-method
    // form; an extension method registers exactly itself. Typed
    // failures: invalid_argument for the unknown_ method, a malformed
    // pattern, or an empty handler; invalid_state for a duplicate
    // (overlapping methods, same pattern, same family) or a registry
    // with no budget; limit_exceeded when the routes capacity is
    // exhausted on this node or an ancestor. On failure the registry is
    // unchanged.
    http::outcome route(const http::method& m, std::string_view pattern,
                        route_handler handler);

    // Registers `handler` for a set of known methods on one pattern.
    // An empty set is invalid_argument; the duplicate rule is
    // overlapping-methods on the same pattern (disjoint sets coexist
    // and the 405 Allow merges them).
    http::outcome route(const http::method_set& set,
                        std::string_view pattern, route_handler handler);

    // Registers `handler` as a prefix-family route: the pattern
    // segments match a leading run of the request path segments (equal
    // length included; "/" is the catch-all). Admission rules match
    // route(); full-match routes outrank prefix routes at resolve time.
    http::outcome route_prefix(const http::method_set& set,
                               std::string_view pattern,
                               route_handler handler);

    // Exact-segment visibility probe over the stored patterns (no
    // capture evaluation); the request matcher arrives with the
    // request-handling tasks.
    bool registered(const http::method& m, const route_pattern& p) const;

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
                       std::string_view route_path) const;

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
    //
    // Observability rule of the full tier: it stops at its first hit
    // (the base match() semantics) unless @p methods_observable is
    // true -- pass true exactly when the merged methods can be
    // observed (route_resolved/before_handler hooks consult
    // route_descriptor.methods); then the tier keeps merging every
    // matching entry's methods past the hit. A method miss carries the
    // full merge either way (the Allow set is always observable).
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
                           std::string_view route_path,
                           bool methods_observable = false) const;

    std::size_t size() const noexcept { return entries_.size(); }

    const resource_budget& budget() const noexcept { return budget_; }

 private:
    explicit route_registry(resource_budget budget) noexcept
        : budget_(std::move(budget)) { }

    struct entry {
        http::method_set methods_;              // known-slot methods
        http::method single_;                   // extension registration
        route_pattern pattern_;
        // The admission-time split of pattern_ (offsets into its
        // text): resolve/match consume this stored form, so no request
        // re-splits an immutable pattern.
        std::vector<detail::pattern_segment> segments_;
        route_handler handler_;
        reservation seat_;
        bool prefix_ = false;
    };

    // True iff the entry would serve @p m: a known method tests the
    // slot set; an extension single compares by identity.
    static bool accepts(const entry& known, const http::method& m) noexcept;

    // The shared admission tail of every registration form: parse,
    // budget gate, same-family duplicate check (overlapping methods on
    // one canonical pattern), then the reserved push.
    http::outcome admit(std::string_view pattern, route_handler handler,
                        bool prefix, const http::method_set& set,
                        const http::method& single);

    // Overlap test for the duplicate rule: two known-slot sets
    // intersect, or two extension singles name the same token. An
    // extension single and a known-slot set are always disjoint.
    static bool overlaps(const entry& known, const http::method_set& set,
                         const http::method& single) noexcept;

    // One tier of the resolve decision. Merges every matching entry's
    // methods (the Allow inputs); the first accepting full match wins,
    // the deepest accepting prefix match wins. The full tier stops at
    // its first hit unless the merged methods are observable
    // (@p methods_observable: route_resolved/before_handler hooks
    // consult route_descriptor.methods -- the dispatcher passes that
    // predicate); the prefix tier always scans fully, because a deeper
    // pattern may still outrank the current best.
    resolve_result resolve_tier(
            const http::method& m,
            const std::vector<std::string_view>& path_segments, bool prefix,
            bool methods_observable) const;

    // Path-shape match of one entry within its tier, over the stored
    // admission-time segments.
    static bool match_tier_entry(
            const entry& known,
            const std::vector<std::string_view>& path_segments, bool prefix,
            std::vector<route_captures>& captures);

    // Folds one matching entry's methods into the tier's Allow inputs.
    static void merge_entry_methods(resolve_result& out,
                                    const entry& known) noexcept;

    // The candidate-selection rule: no winner yet always takes; the
    // full tier keeps its first winner; the prefix tier takes only a
    // strictly deeper pattern.
    static bool outranks(const entry* best, const entry& candidate,
                         bool prefix) noexcept;

    // No accepting entry: a miss unless the tier matched some pattern
    // shape (then the merged methods feed the 405 Allow).
    static resolve_result miss_or_method_miss(resolve_result out) noexcept;

    std::vector<entry> entries_;
    resource_budget budget_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_ROUTES_HPP_
