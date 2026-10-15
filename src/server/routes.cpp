/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/server/routes.hpp>
#include <string>
#include <utility>
#include <vector>

namespace httpserver::server::detail {

http::outcome parse_route_segment(std::string_view segment, std::vector<std::string_view>& names, std::size_t& parameters) {
    if (segment.empty()) {
        return http::outcome(http::outcome_code::invalid_argument, "route_pattern: empty segment (repeated slash)");
    }
    if (segment.front() != '{') {
        // Literal segment: the RFC 9110 token grammar, which excludes
        // braces, spaces, and separators.
        if (!http::detail::is_token(segment)) {
            return http::outcome(http::outcome_code::invalid_argument, "route_pattern: segment '" + std::string(segment) + "' is not a valid literal token");
        }
        return http::outcome::okay();
    }
    if (segment.size() < 3 || segment.back() != '}') {
        return http::outcome(http::outcome_code::invalid_argument,
                             "route_pattern: a parameter segment must be exactly one"
                             " '{name}'");
    }
    const std::string_view name = segment.substr(1, segment.size() - 2);
    if (!valid_parameter_name(name)) {
        return http::outcome(http::outcome_code::invalid_argument, "route_pattern: parameter name '" + std::string(name) + "' must match [A-Za-z0-9_]+");
    }
    for (const std::string_view seen : names) {
        if (seen == name) {
            return http::outcome(http::outcome_code::invalid_argument, "route_pattern: duplicate parameter name '" + std::string(name) + "'");
        }
    }
    names.push_back(name);
    ++parameters;
    return http::outcome::okay();
}

std::vector<std::string_view> split_path_segments(std::string_view path) {
    std::vector<std::string_view> segments;
    if (path.size() < 2) return segments;  // "" and "/"
    std::size_t pos = 1;                   // skip the leading slash
    while (pos < path.size()) {
        const std::size_t slash = path.find('/', pos);
        const std::size_t width = slash == std::string_view::npos ? std::string_view::npos : slash - pos;
        segments.push_back(path.substr(pos, width));
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    return segments;
}

std::vector<pattern_segment> index_path_segments(std::string_view path) {
    std::vector<pattern_segment> segments;
    if (path.size() < 2) return segments;  // "" and "/"
    std::size_t pos = 1;                   // skip the leading slash
    while (pos < path.size()) {
        const std::size_t slash = path.find('/', pos);
        const std::size_t length = slash == std::string_view::npos ? path.size() - pos : slash - pos;
        segments.push_back(pattern_segment{static_cast<std::uint32_t>(pos), static_cast<std::uint32_t>(length)});
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    return segments;
}

bool match_stored_segments(std::string_view pattern_text, const std::vector<pattern_segment>& pattern_segments, bool prefix, const std::vector<std::string_view>& path_segments,
                           std::vector<route_captures>& captures) {
    const std::size_t count = pattern_segments.size();
    if (prefix ? count > path_segments.size() : count != path_segments.size()) {
        return false;
    }
    captures.clear();
    for (std::size_t i = 0; i < count; ++i) {
        const pattern_segment& stored = pattern_segments[i];
        const std::string_view pattern = pattern_text.substr(stored.offset, stored.length);
        const std::string_view segment = path_segments[i];
        if (pattern.front() == '{') {
            if (segment.empty()) return false;
            captures.push_back(route_captures{pattern.substr(1, pattern.size() - 2), std::string(segment)});
            continue;
        }
        if (pattern != segment) return false;
    }
    return true;
}

}  // namespace httpserver::server::detail

namespace httpserver::server {

auto route_pattern::parse(std::string_view text, route_pattern& out) -> http::outcome {
    if (text.empty()) {
        return http::outcome(http::outcome_code::invalid_argument, "route_pattern: pattern is empty");
    }
    if (text.front() != '/') {
        return http::outcome(http::outcome_code::invalid_argument, "route_pattern: pattern must begin with '/'");
    }
    route_pattern parsed;
    if (text != "/") {
        if (text.back() == '/') {
            return http::outcome(http::outcome_code::invalid_argument, "route_pattern: pattern must not end with '/'");
        }
        std::vector<std::string_view> names;
        std::size_t pos = 1;
        while (pos <= text.size()) {
            const std::size_t slash = text.find('/', pos);
            const std::size_t width = slash == std::string_view::npos ? std::string_view::npos : slash - pos;
            if (const http::outcome result = detail::parse_route_segment(text.substr(pos, width), names, parsed.parameters_); !result.ok()) {
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

}  // namespace httpserver::server

/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */

namespace httpserver::server {

auto route_registry::create(const resource_budget& budget, route_registry& out) -> http::outcome {
    if (budget.capacity(resource::routes) == 0) {
        return http::outcome(http::outcome_code::invalid_argument, "route_registry: the budget carries no routes capacity");
    }
    out = route_registry(budget);
    return http::outcome::okay();
}

auto route_registry::route(const http::method& m, std::string_view pattern, route_handler handler) -> http::outcome {
    if (!m.valid()) {
        return http::outcome(http::outcome_code::invalid_argument, "route_registry: the unknown_ method is not registrable");
    }
    if (!handler) {
        return http::outcome(http::outcome_code::invalid_argument, "route_registry: handler is empty");
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

auto route_registry::route(const http::method_set& set, std::string_view pattern, route_handler handler) -> http::outcome {
    if (!set.any()) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "route_registry: a method-set route needs at least one"
                             " method");
    }
    if (!handler) {
        return http::outcome(http::outcome_code::invalid_argument, "route_registry: handler is empty");
    }
    return admit(pattern, std::move(handler), false, set, http::method{});
}

auto route_registry::route_prefix(const http::method_set& set, std::string_view pattern, route_handler handler) -> http::outcome {
    if (!set.any()) {
        return http::outcome(http::outcome_code::invalid_argument,
                             "route_registry: a prefix route needs at least one"
                             " method");
    }
    if (!handler) {
        return http::outcome(http::outcome_code::invalid_argument, "route_registry: handler is empty");
    }
    return admit(pattern, std::move(handler), true, set, http::method{});
}

auto route_registry::registered(const http::method& m, const route_pattern& p) const -> bool {
    for (const entry& known : entries_) {
        if (known.pattern_.text() == p.text() && accepts(known, m)) {
            return true;
        }
    }
    return false;
}

auto route_registry::match(const http::method& m, std::string_view route_path) const -> match_result {
    match_result found;
    const std::vector<std::string_view> path_segments = detail::split_path_segments(route_path);
    for (const entry& known : entries_) {
        if (known.prefix_ || !accepts(known, m)) continue;
        std::vector<route_captures> captures;
        if (!detail::match_stored_segments(known.pattern_.text(), known.segments_, false, path_segments, captures)) {
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

auto route_registry::resolve(const http::method& m, std::string_view route_path, bool methods_observable) const -> resolve_result {
    const std::vector<std::string_view> path_segments = detail::split_path_segments(route_path);
    resolve_result full = resolve_tier(m, path_segments, false, methods_observable);
    if (full.kind != resolve_result::resolve_kind::miss) return full;
    return resolve_tier(m, path_segments, true, methods_observable);
}

auto route_registry::accepts(const entry& known, const http::method& m) noexcept -> bool {
    if (known.single_.valid()) return known.single_ == m;
    return known.methods_.contains(m);
}

auto route_registry::admit(std::string_view pattern, route_handler handler, bool prefix, const http::method_set& set, const http::method& single) -> http::outcome {
    route_pattern parsed;
    if (const http::outcome result = route_pattern::parse(pattern, parsed); !result.ok()) {
        return result;
    }
    if (!budget_.valid()) {
        return http::outcome(http::outcome_code::invalid_state, "route_registry: registry has no budget");
    }
    const std::string& canonical = parsed.text();
    for (const entry& known : entries_) {
        if (known.prefix_ == prefix && known.pattern_.text() == canonical && overlaps(known, set, single)) {
            const std::string label = single.valid() ? std::string(single.name()) : http::to_string(set);
            return http::outcome(http::outcome_code::invalid_state, "route_registry: duplicate route for " + label + " " + canonical);
        }
    }
    reservation seat;
    if (const http::outcome result = budget_.reserve(resource::routes, 1, seat); !result.ok()) {
        return result;
    }
    // Computed before the push: a braced-init-list evaluates
    // left-to-right, so the split must not read the pattern after
    // the move into the entry.
    std::vector<detail::pattern_segment> segments = detail::index_path_segments(canonical);
    entries_.push_back(entry{set, single, std::move(parsed), std::move(segments), std::move(handler), std::move(seat), prefix});
    return http::outcome::okay();
}

auto route_registry::overlaps(const entry& known, const http::method_set& set, const http::method& single) noexcept -> bool {
    if (single.valid()) {
        return known.single_.valid() && known.single_ == single;
    }
    for (std::size_t id = 0; id < static_cast<std::size_t>(http::method_id::extension); ++id) {
        const http::method slot = http::method::known(static_cast<http::method_id>(id));
        if (set.contains(slot) && known.methods_.contains(slot)) {
            return true;
        }
    }
    return false;
}

auto route_registry::resolve_tier(const http::method& m, const std::vector<std::string_view>& path_segments, bool prefix, bool methods_observable) const -> resolve_result {
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
        // The full tier's winner is always its FIRST hit: once the
        // merged methods cannot be observed, the scan stops (the
        // pre-task match() semantics; the merge feeds nothing).
        if (!prefix && !methods_observable) break;
    }
    if (best == nullptr) return miss_or_method_miss(out);
    out.kind = resolve_result::resolve_kind::hit;
    out.handler = &best->handler_;
    out.captures = std::move(best_captures);
    out.pattern_text = best->pattern_.text();
    out.is_prefix = prefix;
    return out;
}

auto route_registry::match_tier_entry(const entry& known, const std::vector<std::string_view>& path_segments, bool prefix, std::vector<route_captures>& captures) -> bool {
    return detail::match_stored_segments(known.pattern_.text(), known.segments_, prefix, path_segments, captures);
}

auto route_registry::merge_entry_methods(resolve_result& out, const entry& known) noexcept -> void {
    if (known.single_.valid()) {
        out.extension_names.emplace_back(known.single_.name());
    } else {
        out.methods.merge(known.methods_);
    }
}

auto route_registry::outranks(const entry* best, const entry& candidate, bool prefix) noexcept -> bool {
    if (best == nullptr) return true;
    if (!prefix) return false;
    return candidate.pattern_.segment_count() > best->pattern_.segment_count();
}

auto route_registry::miss_or_method_miss(resolve_result out) noexcept -> resolve_result {
    if (out.methods.any() || !out.extension_names.empty()) {
        out.kind = resolve_result::resolve_kind::method_miss;
    }
    return out;
}

}  // namespace httpserver::server
