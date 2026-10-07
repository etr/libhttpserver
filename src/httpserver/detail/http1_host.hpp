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

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_HOST_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_HOST_HPP_
#include <string_view>
#include <httpserver/detail/http1_target.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/net/address.hpp>
namespace httpserver::detail {
// RFC 9112 section 3.2: routing may start only after Host cardinality
// and authority syntax are checked. An empty Host is legal for a target
// without an authority; duplicate Host is never unambiguous.
namespace http1_host {
inline bool alphanumeric(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
inline bool name_character(char c) {
    return alphanumeric(c) || std::string_view("-._~!$&'()*+,;=").find(c) != std::string_view::npos;
}
inline bool reg_name(std::string_view name) {
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%') {
            if (name.size() - i < 3 || hex_digit_value(name[i + 1]) < 0 || hex_digit_value(name[i + 2]) < 0) return false;
            i += 2;
        } else if (!name_character(name[i])) {
            return false;
        }
    }
    return true;
}
inline bool future_literal(std::string_view literal) {
    auto dot = literal.find('.');
    if (dot == std::string_view::npos || dot <= 1 || dot + 1 == literal.size()) return false;
    for (char c : literal.substr(1, dot - 1)) if (hex_digit_value(c) < 0) return false;
    for (char c : literal.substr(dot + 1)) if (!name_character(c) && c != ':') return false;
    return true;
}
inline bool ip_literal(std::string_view literal) {
    if (literal.empty()) return false;
    if (literal.front() == 'v' || literal.front() == 'V') return future_literal(literal);
    return literal.find(':') != std::string_view::npos && net::parse_address(literal).has_value();
}
inline bool port(std::string_view value) {
    for (char c : value) if (c < '0' || c > '9') return false;
    return true;
}
}  // namespace http1_host
inline bool valid_http1_host(std::string_view host) {
    if (host.starts_with("[")) {
        auto end = host.find(']');
        if (end == std::string_view::npos || !http1_host::ip_literal(host.substr(1, end - 1))) return false;
        auto suffix = host.substr(end + 1);
        return suffix.empty() || (suffix.front() == ':' && http1_host::port(suffix.substr(1)));
    }
    auto colon = host.find(':');
    if (colon == std::string_view::npos) return http1_host::reg_name(host);
    return http1_host::reg_name(host.substr(0, colon)) && http1_host::port(host.substr(colon + 1));
}
inline bool valid_http1_host(const http::request_head& head) {
    auto hosts = head.head_fields.all("host");
    if (hosts.size() > 1 || (head.request_protocol == http::protocol::http_1_1 && hosts.empty())) return false;
    return hosts.empty() || valid_http1_host(hosts.front());
}
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_HOST_HPP_
