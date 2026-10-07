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

#include <httpserver/detail/http1_websocket_handshake.hpp>
#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <httpserver/detail/base64.hpp>
#include <httpserver/detail/sha1.hpp>
#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/net/address.hpp>
namespace httpserver::detail {
namespace {
constexpr std::size_t k_policy_entries = 256;
using namespace websocket_handshake;  // NOLINT(build/namespaces)
using detail_head::trim_ows;
using detail_head::ascii_iequals;

// Upgrade is a list of protocol-name[/protocol-version], unlike the
// bare-token lists used by Connection and Sec-WebSocket-Protocol.
bool upgrade_protocols(std::span<const std::string> fields, bool& websocket) {
    std::size_t count = 0;
    for (const auto& field : fields) {
        std::string_view rest = field;
        for (;;) {
            const auto comma = rest.find(',');
            const auto entry = trim_ows(rest.substr(0, comma));
            const auto slash = entry.find('/');
            if (!http::detail::is_token(entry.substr(0, slash)) ||
                (slash != std::string_view::npos && !http::detail::is_token(entry.substr(slash + 1))) ||
                count == k_policy_entries) return false;
            ++count;
            if (slash == std::string_view::npos && ascii_iequals(entry, "websocket")) websocket = true;
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    return true;
}
bool contains(const std::vector<std::string_view>& list, std::string_view token) {
    return std::any_of(list.begin(), list.end(), [token](auto value) { return ascii_iequals(value, token); });
}

http1_websocket_plan request_identity(const http::request_head& head) {
    if (head.request_protocol != http::protocol::http_1_1 || head.request_method != http::method::known(http::method_id::get)) return refuse("WebSocket requires HTTP/1.1 GET");
    const auto& fields = head.head_fields;
    if (fields.count("host") != 1 || !authority(trim_ows(*fields.first("host")))) return refuse("invalid Host");
    return {};
}
http1_websocket_plan opening_tokens(const http::fields& fields) {
    std::vector<std::string_view> connections;
    bool websocket = false;
    if (!upgrade_protocols(fields.all("upgrade"), websocket) || !websocket ||
        !tokens(fields.all("connection"), connections, false) || !contains(connections, "upgrade")) return refuse("invalid upgrade tokens");
    return {};
}
http1_websocket_plan request_key(const http::fields& fields) {
    if (fields.count("sec-websocket-key") != 1) return refuse("one key required");
    auto key = trim_ows(*fields.first("sec-websocket-key"));
    if (key.size() != 24) return refuse("invalid key length");
    auto decoded = base64_decode(key);
    if (!decoded || decoded->size() != 16) return refuse("invalid key");
    return {};
}
http1_websocket_plan body_metadata(const http::fields& fields) {
    if (fields.count("transfer-encoding") || fields.count("expect")) return refuse("body metadata conflicts with upgrade");
    if (fields.count("content-length") > 1 || (fields.count("content-length") && trim_ows(*fields.first("content-length")) != "0")) return refuse("upgrade body must be empty");
    return {};
}

http1_websocket_plan validate_request(const http::request_head& head, const ws_upgrade_options& options, std::size_t bytes, std::size_t count) {
    // Sequential checks retain the refusal precedence, without evaluating
    // a later check against fields whose cardinality failed earlier.
    auto result = head_bounds(head.head_fields, bytes, count);
    if (!result.status.ok()) return result;
    result = request_identity(head);
    if (!result.status.ok()) return result;
    result = opening_tokens(head.head_fields);
    if (!result.status.ok()) return result;
    result = request_key(head.head_fields);
    if (!result.status.ok()) return result;
    result = body_metadata(head.head_fields);
    if (!result.status.ok()) return result;
    return request_origin(head.head_fields, options);
}
}  // namespace
http1_websocket_plan negotiate_http1_websocket(const http::request_head& head,
        const ws_upgrade_options& options, std::size_t max_bytes, std::size_t max_fields) {
    auto policy = policy_valid(options, max_bytes);
    if (!policy.ok()) return refuse(policy.message(), 400, policy.code());
    auto out = validate_request(head, options, max_bytes, max_fields);
    if (!out.status.ok()) return out;
    out = select_protocol(head.head_fields, options);
    if (!out.status.ok()) return out;
    auto version = request_version(head.head_fields);
    if (!version.status.ok()) return version;
    std::string composed(trim_ows(*head.head_fields.first("sec-websocket-key")));
    composed += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    out.accept = base64_encode(sha1({reinterpret_cast<const std::byte*>(composed.data()), composed.size()}));
    return out;
}
}  // namespace httpserver::detail
