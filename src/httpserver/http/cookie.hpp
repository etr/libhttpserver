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

#ifndef SRC_HTTPSERVER_HTTP_COOKIE_HPP_
#define SRC_HTTPSERVER_HTTP_COOKIE_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/http/outcome.hpp>

namespace httpserver {
namespace http {

enum class same_site_mode : std::uint8_t { unset, strict, lax, none };

// Owned raw value. Parsing and direct mutation may retain unsafe bytes;
// render_set_cookie is the mandatory validation boundary before emission.
struct cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    std::optional<std::int64_t> expires;
    std::optional<std::int64_t> max_age;
    bool secure = false;
    bool http_only = false;
    same_site_mode same_site = same_site_mode::unset;
};

// Semicolon-separated, first '=' split, SP/HT token/name trimming, one
// outer quote pair removed, malformed/empty-name tokens omitted. Values
// remain byte-transparent and owned; callers apply existing field budgets.
std::vector<cookie> parse_cookie_header(std::string_view header);

// Canonical Expires/Max-Age/Domain/Path/Secure/HttpOnly/SameSite order.
// Invalid bytes, enum or Expires outside years 0000..9999 return
// invalid_argument without changing output. Expires is Unix seconds, GMT.
// SameSite=None implies Secure. Allocation failures may throw.
outcome render_set_cookie(const cookie& value, std::string& output);

}  // namespace http
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_COOKIE_HPP_
