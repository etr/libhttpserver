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

#ifndef SRC_HTTPSERVER_HTTP_PROTOCOL_HPP_
#define SRC_HTTPSERVER_HTTP_PROTOCOL_HPP_

#include <cstdint>
#include <optional>
#include <string_view>

namespace httpserver {

namespace http {

// HTTP protocol version, library-owned (DR-V3-001). No OS or backend
// enum types. Upgrade-based subprotocols (anything negotiated with a
// 101 Switching Protocols response) live within a protocol version;
// they are not protocol values of their own.
enum class protocol : std::uint8_t {
    http_1_0,
    http_1_1,
    http_2,
    http_3,
};

// Wire form of the protocol version; empty string_view for values
// outside the enum range (total over the whole uint8_t domain).
constexpr std::string_view to_string(protocol p) noexcept {
    switch (p) {
        case protocol::http_1_0: return "HTTP/1.0";
        case protocol::http_1_1: return "HTTP/1.1";
        case protocol::http_2: return "HTTP/2";
        case protocol::http_3: return "HTTP/3";
    }
    return {};
}

// Exact-token parse of a protocol version string ("HTTP/1.0",
// "HTTP/1.1", "HTTP/2", "HTTP/3"). Case sensitive; nullopt for any
// other input.
constexpr std::optional<protocol> parse(std::string_view v) noexcept {
    if (v == "HTTP/1.0") return protocol::http_1_0;
    if (v == "HTTP/1.1") return protocol::http_1_1;
    if (v == "HTTP/2") return protocol::http_2;
    if (v == "HTTP/3") return protocol::http_3;
    return std::nullopt;
}

}  // namespace http

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_PROTOCOL_HPP_
