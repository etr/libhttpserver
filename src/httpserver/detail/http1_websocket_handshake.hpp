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

#if !defined(HTTPSERVER_COMPILATION)
#error "http1_websocket_handshake.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_WEBSOCKET_HANDSHAKE_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_WEBSOCKET_HANDSHAKE_HPP_
#include <string>
#include <httpserver/exchange.hpp>
namespace httpserver::detail {
struct http1_websocket_plan {
    http::outcome status;
    std::string accept, selected_subprotocol;
    http::status rejection_status = http::status::from_code(400);
    http::fields rejection_fields;
};
http1_websocket_plan negotiate_http1_websocket(const http::request_head& head,
    const ws_upgrade_options& options, std::size_t max_bytes = 65536,
    std::size_t max_fields = 128);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_WEBSOCKET_HANDSHAKE_HPP_
