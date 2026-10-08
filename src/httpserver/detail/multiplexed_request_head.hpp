/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "multiplexed_request_head.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_MULTIPLEXED_REQUEST_HEAD_HPP_
#define SRC_HTTPSERVER_DETAIL_MULTIPLEXED_REQUEST_HEAD_HPP_
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <httpserver/detail/http1_host.hpp>
#include <httpserver/detail/http1_target.hpp>
#include <httpserver/http/request_head.hpp>
namespace httpserver::detail::multiplexed_head {
struct connect_metadata {
    std::optional<std::string> protocol;
    std::string scheme;
};
inline bool valid_value(std::string_view value) {
    if (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.back() == ' ' || value.back() == '\t')) return false;
    for (unsigned char c : value)
        if ((c < 32 && c != '\t') || c == 127) return false;
    return true;
}
inline bool forbidden(std::string_view name) {
    constexpr std::array<std::string_view, 5> names{"connection", "proxy-connection", "keep-alive", "transfer-encoding", "upgrade"};
    return std::find(names.begin(), names.end(), name) != names.end();
}
struct pseudo_fields {
    std::array<std::optional<std::string_view>, 5> values;
    bool regular = false;
    template <typename Field>
    bool append(const Field& field, bool allow_connect) {
        if (!allow_connect && field.name == ":protocol") return false;
        if (regular) return false;
        constexpr std::array<std::string_view, 5> names{":method", ":scheme", ":path", ":authority", ":protocol"};
        auto found = std::find(names.begin(), names.end(), field.name);
        if (found == names.end()) return false;
        auto& value = values[found - names.begin()];
        if (value || !valid_value(field.value)) return false;
        value = field.value;
        return true;
    }
};
inline bool authority_matches(const pseudo_fields& pseudo, const http::request_head& head) {
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
    return std::equal(hosts.front().begin(), hosts.front().end(), authority->begin(), [](char a, char b) { return http::detail::ascii_upper(a) == http::detail::ascii_upper(b); });
}
inline bool set_authority(const pseudo_fields& pseudo, http::request_head& head) {
    if (!authority_matches(pseudo, head)) return false;
    // Existing shared routes inspect Host. Preserve received ordinary-field
    // order, appending effective :authority only when no Host was supplied.
    if (pseudo.values[3] && !head.head_fields.first("host")) head.head_fields.append("host", *pseudo.values[3]);
    return true;
}
inline bool set_connect_target(const pseudo_fields& pseudo, http::request_head& head) {
    if (pseudo.values[1] || pseudo.values[2] || !pseudo.values[3]) return false;
    head.raw_target = *pseudo.values[3];
    return true;
}
inline bool valid_scheme(std::string_view scheme) {
    if (scheme.empty()) return false;
    const char first = scheme.front();
    const bool alphabetic = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
    if (!alphabetic) return false;
    return std::all_of(scheme.begin() + 1, scheme.end(), [](char c) { return http1_host::alphanumeric(c) || c == '+' || c == '-' || c == '.'; });
}
inline bool set_origin_target(const pseudo_fields& pseudo, http::request_head& head) {
    const auto& scheme = pseudo.values[1];
    const auto& path = pseudo.values[2];
    if (!scheme || !path || path->empty()) return false;
    if (!valid_scheme(*scheme)) return false;
    if (path->front() != '/' && *path != "*") return false;
    if (path->find('#') != std::string_view::npos) return false;
    head.raw_target = *path;
    return true;
}
inline bool set_extended_target(const pseudo_fields& pseudo, http::request_head& head) {
    if (head.request_method.id() != http::method_id::connect || !http::detail::is_token(*pseudo.values[4]) || !pseudo.values[3]) return false;
    if (!set_origin_target(pseudo, head) || head.raw_target.front() != '/') return false;
    // Extended CONNECT routes an origin path while retaining its method.
    return http1_target::derive_route_path(http::method::known(http::method_id::get), head.raw_target, head.route_path).ok();
}
inline bool set_target(const pseudo_fields& pseudo, http::request_head& head, bool allow_connect) {
    const auto& method = pseudo.values[0];
    if (!method) return false;
    auto parsed = http::method::parse(*method);
    if (!parsed) return false;
    head.request_method = *parsed;
    if (!allow_connect && head.request_method.id() == http::method_id::connect) return false;
    if (pseudo.values[4]) return set_extended_target(pseudo, head);
    const bool valid = head.request_method.id() == http::method_id::connect ? set_connect_target(pseudo, head) : set_origin_target(pseudo, head);
    return valid && http1_target::derive_route_path(head.request_method, head.raw_target, head.route_path).ok();
}
inline bool regular_field(std::string_view name, std::string_view value) {
    if (!http::detail::is_token(name) || forbidden(name)) return false;
    for (char c : name)
        if (c >= 'A' && c <= 'Z') return false;
    if (!valid_value(value)) return false;
    return name != "te" || value == "trailers";
}
inline bool content_length(const http::fields& fields, std::optional<std::uint64_t>& length) {
    for (const auto& value : fields.all("content-length")) {
        if (value.empty()) return false;
        std::uint64_t n = 0;
        for (char c : value) {
            if (c < '0' || c > '9') return false;
            const auto digit = static_cast<unsigned>(c - '0');
            if (n > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
            n = n * 10 + digit;
        }
        if (length && *length != n) return false;
        length = n;
    }
    return true;
}
inline bool trailer_field(std::string_view name, std::string_view value) {
    return regular_field(name, value) && name != "content-length" && name != "host" && name != "te" && name != "trailer";
}
template <typename Field>
inline bool append_request_fields(std::span<const Field> fields, http::request_head& head, pseudo_fields& pseudo, bool allow_connect) {
    for (const auto& field : fields) {
        if (field.name.starts_with(":")) {
            if (!pseudo.append(field, allow_connect)) return false;
        } else {
            pseudo.regular = true;
            if (!regular_field(field.name, field.value)) return false;

            head.head_fields.append(field.name, field.value);
        }
    }
    return true;
}
template <typename Field>
inline bool convert(std::span<const Field> fields, http::request_head& head, http::protocol protocol, connect_metadata* connect) {
    pseudo_fields pseudo;
    if (!append_request_fields(fields, head, pseudo, connect != nullptr)) return false;
    head.request_protocol = protocol;
    std::optional<std::uint64_t> length;
    if (!set_target(pseudo, head, connect != nullptr) || !set_authority(pseudo, head) || !content_length(head.head_fields, length)) return false;
    if (connect && pseudo.values[4]) connect->protocol = std::string(*pseudo.values[4]);
    if (connect && pseudo.values[1]) connect->scheme = *pseudo.values[1];
    return true;
}
}  // namespace httpserver::detail::multiplexed_head
#endif  // SRC_HTTPSERVER_DETAIL_MULTIPLEXED_REQUEST_HEAD_HPP_
