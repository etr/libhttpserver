/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_CONSTANTS_HPP_
#define SRC_HTTPSERVER_CONSTANTS_HPP_

#include <cstdint>
#include <string_view>

// Named, stable replacements for the v1 #define constant wall. Eliminates
// macro pollution from public headers while preserving v1 identifiers for
// mechanical migration — only the namespace qualifier changes at call sites.
namespace httpserver::constants {

// Default TCP port the webserver binds to when no port() is set.
inline constexpr std::uint16_t DEFAULT_WS_PORT = 9898;

// Default per-connection timeout in seconds. Type is int to match the
// create_webserver._connection_timeout field (avoids -Wconversion at the
// assignment site). Callers must pass non-negative values; the builder
// validates this at runtime.
inline constexpr int DEFAULT_WS_TIMEOUT = 180;

// Bitmask sentinel used by ip_representation when no explicit CIDR mask
// has been parsed (all 16 nibbles "present").
inline constexpr std::uint16_t DEFAULT_MASK_VALUE = 0xFFFFu;

// Default body for a 404 response when no not_found_handler is set.
// std::string_view is non-allocating; callers convert to std::string as needed.
inline constexpr std::string_view NOT_FOUND_ERROR = "Not Found";

// Default body for a 405 response when no method_not_allowed_handler is set.
// Named METHOD_ERROR (not METHOD_NOT_ALLOWED_ERROR) to preserve the v1 macro
// spelling exactly — the namespacing is the API change, not a rename.
inline constexpr std::string_view METHOD_ERROR = "Method not Allowed";

// Default body for a 406 response. Retained for v1 API parity.
inline constexpr std::string_view NOT_METHOD_ERROR = "Method not Acceptable";

// v1 API-parity constant. Retained for external consumers that may
// reference the exact string "Internal Error"; the live 500 dispatch
// path uses INTERNAL_SERVER_ERROR (DR-009 Revision 1), below.
inline constexpr std::string_view GENERIC_ERROR = "Internal Error";

// TASK-055 / DR-009 Revision 1: fixed, sanitized default body for the
// "no internal_error_handler configured, and expose_exception_messages
// is false" path in webserver_impl::internal_error_page. This is the
// CWE-209 fix: the originating exception's e.what() text MUST NOT cross
// a process boundary by default. The verbose body (carrying msg) is
// restored only when expose_exception_messages(true) is set on the
// builder, which is documented as development-only.
inline constexpr std::string_view INTERNAL_SERVER_ERROR =
    "Internal Server Error";

}  // namespace httpserver::constants

#endif  // SRC_HTTPSERVER_CONSTANTS_HPP_
