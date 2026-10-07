/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_websocket_handshake.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_WEBSOCKET_HANDSHAKE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_WEBSOCKET_HANDSHAKE_HPP_
#include <httpserver/detail/websocket_handshake.hpp>
#include <httpserver/detail/http2_request_head.hpp>
namespace httpserver::detail {
websocket_handshake_plan negotiate_http2_websocket(const http::request_head& head, const http2_connect_metadata& connect,
    const ws_upgrade_options& options, std::size_t max_bytes, std::size_t max_fields);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_WEBSOCKET_HANDSHAKE_HPP_
