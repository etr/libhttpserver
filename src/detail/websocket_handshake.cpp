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

#include <algorithm>
#include <charconv>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/websocket_handshake.hpp>
#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/net/address.hpp>
namespace httpserver::detail::websocket_handshake {
constexpr std::size_t k_policy_entries = 256;
using detail_head::trim_ows;
websocket_handshake_plan refuse(std::string reason, unsigned code,
        http::outcome_code kind) {
    websocket_handshake_plan out;
    out.status = {kind, std::move(reason)};
    out.rejection_status = http::status::from_code(code);
    if (code == 426) out.rejection_fields.append("Sec-WebSocket-Version", "13");
    return out;
}
bool valid_port(std::string_view port) {
    unsigned value = 0;
    auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), value);
    return error == std::errc() && end == port.data() + port.size() && value > 0 && value <= 65535;
}
bool hostname(std::string_view host) {
    if (host.empty()) return false;
    return host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.") == std::string_view::npos;
}
bool authority(std::string_view text) {
    if (text.empty()) return false;
    std::size_t colon;
    if (text.front() == '[') {
        const auto end = text.find(']');
        if (end == std::string_view::npos) return false;
        auto ip = net::parse_address(text.substr(1, end - 1));
        if (!ip || ip->family != net::address_family::ipv6) return false;
        colon = end + 1;
        if (colon < text.size() && text[colon] != ':') return false;
    } else {
        colon = text.find(':');
        if (!hostname(text.substr(0, colon))) return false;
    }
    if (colon >= text.size()) return true;
    return valid_port(text.substr(colon + 1));
}
bool origin(std::string_view text) {
    if (text == "null") return true;
    if (text.starts_with("https://")) text.remove_prefix(8);
    else if (text.starts_with("http://")) text.remove_prefix(7);
    else return false;
    return authority(text);
}
bool tokens(std::span<const std::string> fields, std::vector<std::string_view>& out,
            bool unique) {
    for (const auto& field : fields) {
        std::string_view rest = field;
        for (;;) {
            const auto comma = rest.find(',');
            auto token = trim_ows(rest.substr(0, comma));
            if (!http::detail::is_token(token)) return false;
            if (unique && std::find(out.begin(), out.end(), token) != out.end()) return false;
            if (out.size() == k_policy_entries) return false;
            out.push_back(token);
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    return true;
}
http::outcome policy_valid(const ws_upgrade_options& options, std::size_t cap) {
    if (std::max(options.subprotocols.size(), options.allowed_origins.size()) > k_policy_entries)
        return {http::outcome_code::invalid_argument, "handshake policy entry limit exceeded"};
    auto valid = options.limits.validate();
    if (!valid.ok()) return valid;
    std::size_t bytes = 0;
    for (const auto& value : options.allowed_origins) {
        if (value.size() > cap - bytes || !origin(value)) return {http::outcome_code::invalid_argument, "invalid origin policy"};
        bytes += value.size();
    }
    std::vector<std::string_view> seen;
    for (const auto& value : options.subprotocols) {
        if (value.size() > cap - bytes || !http::detail::is_token(value) || std::find(seen.begin(), seen.end(), value) != seen.end())
            return {http::outcome_code::invalid_argument, "invalid subprotocol policy"};
        bytes += value.size(); seen.push_back(value);
    }
    return http::outcome::okay();
}
websocket_handshake_plan head_bounds(const http::fields& fields, std::size_t max_bytes, std::size_t max_fields) {
    if (fields.size() > max_fields) return refuse("handshake field limit", 400, http::outcome_code::limit_exceeded);
    std::size_t bytes = 0;
    for (auto field : fields.entries()) {
        if (field.name.size() > max_bytes - bytes) return refuse("handshake byte limit", 400, http::outcome_code::limit_exceeded);
        bytes += field.name.size();
        if (field.value.size() > max_bytes - bytes) return refuse("handshake byte limit", 400, http::outcome_code::limit_exceeded);
        bytes += field.value.size();
    }
    return {};
}
websocket_handshake_plan request_origin(const http::fields& fields, const ws_upgrade_options& options) {
    if (fields.count("origin") > 1) return refuse("one origin permitted");
    if (!fields.count("origin")) {
        if (!options.allow_absent_origin) return refuse("absent origin denied", 403);
        return {};
    }
    auto value = trim_ows(*fields.first("origin"));
    if (!origin(value)) return refuse("invalid origin");
    if (!options.allowed_origins.empty() && std::find(options.allowed_origins.begin(), options.allowed_origins.end(), value) == options.allowed_origins.end()) return refuse("origin denied", 403);
    return {};
}
websocket_handshake_plan request_version(const http::fields& fields) {
    if (fields.count("sec-websocket-version") != 1) return refuse("one version required");
    auto version = trim_ows(*fields.first("sec-websocket-version"));
    unsigned value = 0;
    auto [end, error] = std::from_chars(version.data(), version.data() + version.size(), value);
    if (version.empty() || error != std::errc() || end != version.data() + version.size()) return refuse("invalid version");
    if (version != "13") return refuse("unsupported WebSocket version", 426);
    return {};
}
websocket_handshake_plan select_protocol(const http::fields& fields, const ws_upgrade_options& options) {
    std::vector<std::string_view> protocols;
    if (!tokens(fields.all("sec-websocket-protocol"), protocols, true)) return refuse("invalid subprotocol offers");
    websocket_handshake_plan out;
    for (const auto& preferred : options.subprotocols) {
        if (std::find(protocols.begin(), protocols.end(), preferred) != protocols.end()) {
            out.selected_subprotocol = preferred;
            break;
        }
    }
    if (out.selected_subprotocol.empty() && options.require_subprotocol) return refuse("no matching subprotocol");
    return out;
}
}  // namespace httpserver::detail::websocket_handshake
