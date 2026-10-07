/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <httpserver/detail/http2_request_head.hpp>
#include <httpserver/detail/http1_host.hpp>
namespace httpserver::detail {
namespace {
bool valid_value(std::string_view value) {
    if (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.back() == ' ' || value.back() == '\t')) return false;
    for (unsigned char c : value) if ((c < 32 && c != '\t') || c == 127) return false;
    return true;
}
bool forbidden(std::string_view name) {
    constexpr std::array<std::string_view, 5> names{"connection", "proxy-connection", "keep-alive", "transfer-encoding", "upgrade"};
    return std::find(names.begin(), names.end(), name) != names.end();
}
bool empty_length(std::string_view value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](char c) { return c == '0'; });
}
struct pseudo_fields {
    std::array<std::optional<std::string_view>, 4> values;
    bool regular = false;
    bool append(const hpack_field& field) {
        if (regular) return false;
        constexpr std::array<std::string_view, 4> names{":method", ":scheme", ":path", ":authority"};
        auto found = std::find(names.begin(), names.end(), field.name);
        if (found == names.end()) return false;
        auto& value = values[found - names.begin()];
        if (value || !valid_value(field.value)) return false;
        value = field.value;
        return true;
    }
};
bool authority_matches(const pseudo_fields& pseudo, const http::request_head& head) {
    const auto& authority = pseudo.values[3];
    auto hosts = head.head_fields.all("host");
    if (hosts.size() > 1) return false;
    if (!hosts.empty() && !valid_http1_host(hosts.front())) return false;
    if (!authority) return true;
    if (authority->empty() || !valid_http1_host(*authority)) return false;
    if (hosts.empty()) return true;
    // Authority comparison follows ASCII host case insensitivity. Port bytes
    // remain part of the identity; no default-port rewriting is performed.
    if (hosts.front().size() != authority->size()) return false;
    return std::equal(hosts.front().begin(), hosts.front().end(), authority->begin(), [](char a, char b) {
        return http::detail::ascii_upper(a) == http::detail::ascii_upper(b);
    });
}
bool set_authority(const pseudo_fields& pseudo, http::request_head& head) {
    if (!authority_matches(pseudo, head)) return false;
    // Existing shared routes inspect Host. Preserve received ordinary-field
    // order, appending effective :authority only when no Host was supplied.
    if (pseudo.values[3] && !head.head_fields.first("host")) head.head_fields.append("host", *pseudo.values[3]);
    return true;
}
bool set_connect_target(const pseudo_fields& pseudo, http::request_head& head) {
    if (pseudo.values[1] || pseudo.values[2] || !pseudo.values[3]) return false;
    head.raw_target = *pseudo.values[3];
    return true;
}
bool valid_scheme(std::string_view scheme) {
    if (scheme.empty()) return false;
    const char first = scheme.front();
    const bool alphabetic = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
    if (!alphabetic) return false;
    return std::all_of(scheme.begin() + 1, scheme.end(), [](char c) {
        return http1_host::alphanumeric(c) || c == '+' || c == '-' || c == '.';
    });
}
bool set_origin_target(const pseudo_fields& pseudo, http::request_head& head) {
    const auto& scheme = pseudo.values[1];
    const auto& path = pseudo.values[2];
    if (!scheme || !path || path->empty()) return false;
    if (!valid_scheme(*scheme)) return false;
    if (path->front() != '/' && *path != "*") return false;
    if (path->find('#') != std::string_view::npos) return false;
    head.raw_target = *path;
    return true;
}
bool set_target(const pseudo_fields& pseudo, http::request_head& head) {
    const auto& method = pseudo.values[0];
    if (!method) return false;
    auto parsed = http::method::parse(*method);
    if (!parsed) return false;
    head.request_method = *parsed;
    const bool valid = head.request_method.id() == http::method_id::connect ?
        set_connect_target(pseudo, head) : set_origin_target(pseudo, head);
    return valid && http1_target::derive_route_path(head.request_method, head.raw_target, head.route_path).ok();
}
}  // namespace
bool http2_regular_field(std::string_view name, std::string_view value) {
    if (!http::detail::is_token(name) || forbidden(name)) return false;
    for (char c : name) if (c >= 'A' && c <= 'Z') return false;
    if (!valid_value(value)) return false;
    return name != "te" || value == "trailers";
}
bool http2_convert_request(std::span<const hpack_field> fields, http::request_head& head) {
    pseudo_fields pseudo;
    for (const auto& field : fields) {
        if (field.name.starts_with(":")) {
            if (!pseudo.append(field)) return false;
        } else {
            pseudo.regular = true;
            if (!http2_regular_field(field.name, field.value)) return false;
            if (field.name == "content-length" && !empty_length(field.value)) return false;
            head.head_fields.append(field.name, field.value);
        }
    }
    head.request_protocol = http::protocol::http_2;
    return set_target(pseudo, head) && set_authority(pseudo, head);
}
}  // namespace httpserver::detail
