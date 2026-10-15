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
bool valid_value(std::string_view value);
bool forbidden(std::string_view name);
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
bool authority_matches(const pseudo_fields& pseudo, const http::request_head& head);
bool set_authority(const pseudo_fields& pseudo, http::request_head& head);
bool set_connect_target(const pseudo_fields& pseudo, http::request_head& head);
bool valid_scheme(std::string_view scheme);
bool set_origin_target(const pseudo_fields& pseudo, http::request_head& head);
bool set_extended_target(const pseudo_fields& pseudo, http::request_head& head);
bool set_target(const pseudo_fields& pseudo, http::request_head& head, bool allow_connect);
bool regular_field(std::string_view name, std::string_view value);
bool content_length(const http::fields& fields, std::optional<std::uint64_t>& length);
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
