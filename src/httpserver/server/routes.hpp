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

// Segment-wise pattern match over an already-split path: a literal
// pattern segment compares equal, a single {name} segment captures any
// non-empty segment, and the segment counts must agree. Captures fill
// in pattern order; a failed match leaves them unused.
inline bool match_path_segments(
    std::string_view pattern_text,
    const std::vector<std::string_view>& path_segments,
    std::vector<std::string>& captures) {
    const std::vector<std::string_view> pattern_segments =
        split_path_segments(pattern_text);
    if (pattern_segments.size() != path_segments.size()) return false;
    captures.clear();
    for (std::size_t i = 0; i < pattern_segments.size(); ++i) {
        const std::string_view& pattern = pattern_segments[i];
        const std::string_view& segment = path_segments[i];
        if (pattern.front() == '{') {
            if (segment.empty()) return false;
            captures.emplace_back(segment);
            continue;
        }
        if (pattern != segment) return false;
    }
    return true;
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

    // Registers `handler` for (method, pattern). Typed failures:
    // invalid_argument for the unknown_ method, a malformed pattern, or
    // an empty handler; invalid_state for a duplicate (method, pattern)
    // pair or a registry with no budget; limit_exceeded when the routes
    // capacity is exhausted on this node or an ancestor. On failure the
    // registry is unchanged.
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
            if (known.method_ == m && known.pattern_.text() == canonical) {
                return http::outcome(
                    http::outcome_code::invalid_state,
                    "route_registry: duplicate route for "
                        + std::string(m.name()) + " " + canonical);
            }
        }
        reservation seat;
        if (const http::outcome result =
                budget_.reserve(resource::routes, 1, seat);
            !result.ok()) {
            return result;
        }
        entries_.push_back(entry{m, std::move(parsed), std::move(handler),
                                 std::move(seat)});
        return http::outcome::okay();
    }

    // Exact-segment visibility probe over the stored patterns (no
    // capture evaluation); the request matcher arrives with the
    // request-handling tasks.
    bool registered(const http::method& m, const route_pattern& p) const {
        for (const entry& known : entries_) {
            if (known.method_ == m && known.pattern_.text() == p.text()) {
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
            if (known.method_ != m) continue;
            std::vector<std::string> captures;
            if (detail::match_path_segments(known.pattern_.text(),
                                            path_segments, captures)) {
                found.handler = &known.handler_;
                found.parameters = std::move(captures);
                break;
            }
        }
        return found;
    }

    std::size_t size() const noexcept { return entries_.size(); }

    const resource_budget& budget() const noexcept { return budget_; }

 private:
    explicit route_registry(resource_budget budget) noexcept
        : budget_(std::move(budget)) { }

    struct entry {
        http::method method_;
        route_pattern pattern_;
        route_handler handler_;
        reservation seat_;
    };

    std::vector<entry> entries_;
    resource_budget budget_;
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_ROUTES_HPP_
