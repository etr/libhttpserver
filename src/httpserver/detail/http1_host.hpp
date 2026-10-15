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
bool name_character(char c);
bool reg_name(std::string_view name);
bool future_literal(std::string_view literal);
bool ip_literal(std::string_view literal);
bool port(std::string_view value);
}  // namespace http1_host
bool valid_http1_host(std::string_view host);
bool valid_http1_host(const http::request_head& head);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_HOST_HPP_
