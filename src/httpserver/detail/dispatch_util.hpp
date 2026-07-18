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

// Shared dispatch-path free functions (DR-014). These are pure helpers
// with no instance state, so they live as free functions in
// httpserver::detail rather than as methods on a service class. Keeping
// log_dispatch_error here (rather than on error_pages) avoids forcing
// every service's error path to take a dependency edge on error_pages.
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "dispatch_util.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DISPATCH_UTIL_HPP_
#define SRC_HTTPSERVER_DETAIL_DISPATCH_UTIL_HPP_

#include <string_view>

namespace httpserver {

struct webserver_config;

namespace detail {

// Forward @p msg VERBATIM to config.log_error if a logger is configured;
// a no-op when none is. Swallows any exception thrown by the user logger
// so a misbehaving logger cannot poison a catch from inside the catch
// (noexcept). @p msg is forwarded unchanged regardless of
// create_webserver::expose_exception_messages — the error log is the
// canonical destination for verbatim exception text; only the HTTP
// response body path is sanitized (CWE-209 / CWE-532; see error_pages).
void log_dispatch_error(const webserver_config& config,
                        std::string_view msg) noexcept;

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DISPATCH_UTIL_HPP_
