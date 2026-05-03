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

// Public, namespaced replacements for the v1 #define wall. Each constant
// here was previously a value-form macro in a public header (see PRD-CFG-
// REQ-002 / architecture §4.9 for the rationale). The identifiers
// preserve their v1 spellings so the migration is mechanical: only the
// namespace qualifier changes at call sites.
//
// `inline constexpr` (C++17+, project floor is C++20 per TASK-001) gives
// each symbol a single ODR-stable definition usable from any TU that
// includes this header.
namespace httpserver::constants {

// Default TCP port the webserver binds to when no `port()` is set on the
// create_webserver builder. Replaces v1 `#define DEFAULT_WS_PORT 9898`.
inline constexpr std::uint16_t DEFAULT_WS_PORT = 9898;

// Default per-connection timeout in seconds. Replaces v1
// `#define DEFAULT_WS_TIMEOUT 180`. Type is `int` to match the
// `create_webserver._connection_timeout` field exactly — no implicit
// conversion at the assignment site, no -Wconversion noise. The value
// is non-negative by construction.
inline constexpr int DEFAULT_WS_TIMEOUT = 180;

// Bitmask sentinel used by ip_representation when no explicit CIDR mask
// has been parsed (all 16 nibbles "present"). Replaces v1
// `#define DEFAULT_MASK_VALUE 0xFFFF`.
inline constexpr std::uint16_t DEFAULT_MASK_VALUE = 0xFFFFu;

// Default body for a 404 response when no not_found_resource is set on
// the webserver. Replaces v1 `#define NOT_FOUND_ERROR "Not Found"`.
// std::string_view keeps storage non-allocating; call sites materialize
// a std::string via the string_response constructor.
inline constexpr std::string_view NOT_FOUND_ERROR = "Not Found";

// Default body for a 405 response when no method_not_allowed_resource
// is set. Replaces v1 `#define METHOD_ERROR "Method not Allowed"`.
// The name is preserved (rather than renamed to METHOD_NOT_ALLOWED_ERROR)
// to keep the migration mechanical — the namespacing is the API change,
// not a rename.
inline constexpr std::string_view METHOD_ERROR = "Method not Allowed";

// Default body for a 406 response. Replaces v1
// `#define NOT_METHOD_ERROR "Method not Acceptable"`. Currently unused
// by any in-tree caller (verified by grep across src/, test/, examples/);
// retained for v1 API parity per the v2.0 mechanical-migration policy.
inline constexpr std::string_view NOT_METHOD_ERROR = "Method not Acceptable";

// Default body for a 500 response when no internal_error_resource is
// set. Replaces v1 `#define GENERIC_ERROR "Internal Error"`.
inline constexpr std::string_view GENERIC_ERROR = "Internal Error";

}  // namespace httpserver::constants

#endif  // SRC_HTTPSERVER_CONSTANTS_HPP_
