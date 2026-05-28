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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units.
#if !defined(HTTPSERVER_COMPILATION)
#error "method_utils.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_METHOD_UTILS_HPP_
#define SRC_HTTPSERVER_DETAIL_METHOD_UTILS_HPP_

#include <cstdint>
#include <string>

#include "httpserver/http_method.hpp"

namespace httpserver {
namespace detail {

// TASK-048 review cleanup (findings 3 & 4): shared free function for
// serializing a method_set into the comma-separated string value expected
// by the HTTP `Allow:` header.
//
// Previously this logic was duplicated as:
//   - webserver_impl::serialize_allow_methods (webserver_dispatch.cpp)
//   - serialize_allow_methods_local (webserver_aliases.cpp, anonymous ns)
//
// Both callers iterate the same enum range with identical logic. This
// single free function replaces both copies; it takes only method_set
// (no webserver_impl* dependency) so it can be called from any TU that
// includes this header.
//
// Enum-declaration order: GET, HEAD, POST, PUT, DELETE, CONNECT,
// OPTIONS, TRACE, PATCH (TASK-021). The only existing assertion in-tree
// is "HEAD, POST" which is preserved by enum order.
inline std::string format_allow_header(method_set allowed) {
    std::string header_value;
    // Pre-size for 9 methods * ~8 chars each (finding #27: avoid
    // reallocation across the 9-method upper bound).
    header_value.reserve(64);
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (!allowed.contains(m)) continue;
        if (!header_value.empty()) header_value += ", ";
        // to_string returns string_view; operator+= on std::string accepts
        // string_view directly -- no temporary std::string needed.
        header_value += to_string(m);
    }
    return header_value;
}

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_METHOD_UTILS_HPP_
