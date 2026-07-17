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

// kForbiddenFieldChars used to be defined
// independently in both src/http_response.cpp and
// src/http_response_factories.cpp (identical value, but a silent-drift
// risk on a security-relevant character set — CWE-113 header injection).
// This header centralizes the single definition both TUs include.

// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units, never from the public umbrella.
//
// This #error check intentionally precedes the include guard below: a
// consumer TU must never reach this file at all, so the check has to fire
// on *every* inclusion attempt, not just the first. The include guard
// below only needs to protect internal TUs that legitimately include this
// header more than once (which is the normal, allowed case); if it were
// placed first, a second internal include would short-circuit past the
// #error and silently hide a first, illegitimate include from a consumer.
#if !defined(HTTPSERVER_COMPILATION)
#error "http_field_validation.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP_FIELD_VALIDATION_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP_FIELD_VALIDATION_HPP_

#include <string_view>

namespace httpserver {
namespace detail {

// Shared forbidden-character set for header/footer/cookie field names and
// values, and for the unauthorized() scheme/realm/digest_challenge fields.
// The string_view spans all three bytes including the embedded NUL; any of
// CR, LF, or NUL can be used to inject additional HTTP headers (CWE-113).
inline constexpr std::string_view kForbiddenFieldChars("\r\n\0", 3);

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP_FIELD_VALIDATION_HPP_
