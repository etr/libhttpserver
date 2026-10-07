/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/http2_websocket_handshake.hpp>
namespace httpserver::detail {
namespace {
bool valid_identity(const http::request_head& head, const http2_connect_metadata& connect) {
    return head.request_protocol == http::protocol::http_2 && head.request_method.id() == http::method_id::connect &&
        connect.protocol == "websocket" && (connect.scheme == "http" || connect.scheme == "https");
}
}  // namespace
websocket_handshake_plan negotiate_http2_websocket(const http::request_head& head, const http2_connect_metadata& connect,
        const ws_upgrade_options& options, std::size_t max_bytes, std::size_t max_fields) {
    using namespace websocket_handshake;  // NOLINT(build/namespaces)
    auto policy = policy_valid(options, max_bytes);
    if (!policy.ok()) return refuse(policy.message(), 400, policy.code());
    auto result = head_bounds(head.head_fields, max_bytes, max_fields);
    if (!result.status.ok()) return result;
    if (!valid_identity(head, connect)) return refuse("invalid WebSocket Extended CONNECT");
    const auto& fields = head.head_fields;
    if (fields.count("content-length") || fields.count("expect") || fields.count("trailer")) return refuse("finite body metadata conflicts with tunnel");
    result = request_origin(fields, options);
    if (!result.status.ok()) return result;
    result = select_protocol(fields, options);
    if (!result.status.ok()) return result;
    auto version = request_version(fields);
    return version.status.ok() ? result : version;
}
}  // namespace httpserver::detail
