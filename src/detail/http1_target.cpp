/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http1_target.hpp>
#include <string>
#include <vector>

namespace httpserver::detail::http1_target {

bool contains_forbidden_byte(std::string_view raw) noexcept {
    for (const char c : raw) {
        if (c == ' ' || c == '#') return true;
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x1F || u == 0x7F) return true;
    }
    return false;
}

char lower_ascii(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') ? static_cast<char>(u - 'A' + 'a') : c;
}

bool equals_folded(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_ascii(a[i]) != lower_ascii(b[i])) return false;
    }
    return true;
}

http::outcome select_connect_target(std::string_view raw, target_selection& out) {
    if (raw.find('/') != std::string_view::npos || raw.find('?') != std::string_view::npos) {
        return http::outcome(http::outcome_code::protocol_error, "CONNECT requires the authority-form request-target");
    }
    out = target_selection{form::authority, std::string_view()};
    return http::outcome::okay();
}

http::outcome select_path_target(std::string_view raw, target_selection& out) {
    if (raw.front() == '/') {
        out = target_selection{form::origin, raw};
        return http::outcome::okay();
    }
    const std::size_t scheme_end = raw.find("://");
    if (scheme_end == std::string_view::npos || !is_http_scheme(raw.substr(0, scheme_end))) {
        return http::outcome(http::outcome_code::protocol_error,
                             "request-target is not a valid origin-, absolute-, or"
                             " authority-form");
    }
    const std::string_view after_scheme = raw.substr(scheme_end + 3);
    const std::size_t authority_end = after_scheme.find_first_of("/?");
    const std::size_t origin_begin = authority_end == std::string_view::npos ? after_scheme.size() : authority_end;
    if (origin_begin == 0 || after_scheme.empty()) {
        return http::outcome(http::outcome_code::protocol_error, "absolute request-target has no authority");
    }
    out = target_selection{form::absolute, after_scheme.substr(origin_begin)};
    return http::outcome::okay();
}

http::outcome select_target_form(const http::method& m, std::string_view raw, target_selection& out) {
    if (raw.empty()) {
        // Migration note (delta 4): v2 canonicalized "" to "/"; v3
        // rejects origin-form targets that do not start with '/'.
        return http::outcome(http::outcome_code::protocol_error, "request-target is empty");
    }
    if (raw == "*") {
        if (m.id() != http::method_id::options) {
            return http::outcome(http::outcome_code::protocol_error, "asterisk-form is valid only for OPTIONS");
        }
        out = target_selection{form::asterisk, std::string_view()};
        return http::outcome::okay();
    }
    if (contains_forbidden_byte(raw)) {
        return http::outcome(http::outcome_code::protocol_error, "forbidden byte in request-target");
    }
    if (m.id() == http::method_id::connect) {
        return select_connect_target(raw, out);
    }
    return select_path_target(raw, out);
}

http::outcome decode_path(std::string_view raw, std::string& out) {
    out.clear();
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        const char c = raw[i];
        if (c == '+') {
            out.push_back(' ');
            ++i;
            continue;
        }
        if (c != '%') {
            out.push_back(c);
            ++i;
            continue;
        }
        if (raw.size() - i <= 2) {
            return http::outcome(http::outcome_code::protocol_error, "truncated percent-escape in path");
        }
        const int high = hex_digit_value(raw[i + 1]);
        const int low = hex_digit_value(raw[i + 2]);
        if (high < 0 || low < 0) {
            return http::outcome(http::outcome_code::protocol_error, "invalid percent-escape in path");
        }
        const char decoded = static_cast<char>((high << 4) | low);
        if (decoded == '\0') {
            return http::outcome(http::outcome_code::protocol_error, "decoded NUL byte in path");
        }
        out.push_back(decoded);
        i += 3;
    }
    return http::outcome::okay();
}

http::outcome resolve_dot_segments(std::string_view path, std::string& route) {
    std::vector<std::string_view> kept;
    std::string_view rest = path;
    while (!rest.empty()) {
        rest.remove_prefix(1);  // the leading slash (path is absolute)
        const std::size_t slash = rest.find('/');
        const std::string_view segment = rest.substr(0, slash == std::string_view::npos ? rest.size() : slash);
        if (segment == "..") {
            if (!kept.empty()) kept.pop_back();
        } else if (keeps_segment(segment)) {
            kept.push_back(segment);
        }
        if (slash == std::string_view::npos) break;
        rest = rest.substr(slash);
    }
    route.clear();
    for (const std::string_view segment : kept) {
        route.push_back('/');
        route.append(segment);
    }
    if (route.empty()) route.push_back('/');
    return http::outcome::okay();
}

http::outcome derive_route_path(const http::method& m, std::string_view raw_target, std::string& route_path) {
    target_selection selection;
    if (const http::outcome selected = select_target_form(m, raw_target, selection); !selected.ok()) {
        return selected;
    }
    switch (selection.shape) {
        case form::asterisk:
            route_path = "/";
            return http::outcome::okay();
        case form::authority:
            route_path = std::string(raw_target);
            return http::outcome::okay();
        case form::origin:
        case form::absolute:
            break;
    }
    const std::size_t query = selection.origin.find('?');
    std::string_view path = selection.origin.substr(0, query);
    if (path.empty()) path = "/";  // absolute-form "http://h"
    std::string decoded;
    if (const http::outcome decoded_out = decode_path(path, decoded); !decoded_out.ok()) {
        return decoded_out;
    }
    return resolve_dot_segments(decoded, route_path);
}

}  // namespace httpserver::detail::http1_target
