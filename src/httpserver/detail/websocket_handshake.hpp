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
#error "websocket_handshake.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_WEBSOCKET_HANDSHAKE_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSOCKET_HANDSHAKE_HPP_
#include <string>
#include <vector>
#include <httpserver/exchange.hpp>
namespace httpserver::detail {
struct websocket_handshake_plan {
    http::outcome status;
    std::string accept, selected_subprotocol;
    http::status rejection_status = http::status::from_code(400);
    http::fields rejection_fields;
};
namespace websocket_handshake {
websocket_handshake_plan refuse(std::string reason, unsigned code = 400, http::outcome_code kind = http::outcome_code::protocol_error);
bool authority(std::string_view text);
bool tokens(std::span<const std::string> fields, std::vector<std::string_view>& out, bool unique);
http::outcome policy_valid(const ws_upgrade_options& options, std::size_t cap);
websocket_handshake_plan head_bounds(const http::fields& fields, std::size_t max_bytes, std::size_t max_fields);
websocket_handshake_plan request_origin(const http::fields& fields, const ws_upgrade_options& options);
websocket_handshake_plan request_version(const http::fields& fields);
websocket_handshake_plan select_protocol(const http::fields& fields, const ws_upgrade_options& options);
}  // namespace websocket_handshake
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_WEBSOCKET_HANDSHAKE_HPP_
