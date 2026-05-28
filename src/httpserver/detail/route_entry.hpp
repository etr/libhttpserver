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

// TASK-025: route table value type as pinned by architecture spec §4.7.
//
// `route_entry` carries the method-set the entry covers, the actual
// handler (either a stateless lambda or a shared_ptr to a class-derived
// http_resource), and a `is_prefix` flag that lets the route table
// distinguish exact-match registrations (register_path / on_*) from
// prefix-match registrations (register_prefix). TASK-027 will plug this
// type into the real 3-tier route table; TASK-025 only ships the type
// and the on_* entry points that build it.
//
// The header is internal — only reachable when compiling libhttpserver
// itself (HTTPSERVER_COMPILATION is supplied via src/Makefile.am
// AM_CPPFLAGS, and via test/Makefile.am for the test TUs that need to
// pin the variant shape with static_assert). It must NOT be included
// from the public umbrella <httpserver.hpp>.
#if !defined(HTTPSERVER_COMPILATION)
#error "route_entry.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_ROUTE_ENTRY_HPP_
#define SRC_HTTPSERVER_DETAIL_ROUTE_ENTRY_HPP_

#include <functional>
#include <memory>
#include <variant>

#include "httpserver/http_method.hpp"

namespace httpserver {
class http_request;
class http_response;
class http_resource;
}  // namespace httpserver

namespace httpserver {
namespace detail {

// The lambda arm of the route_entry payload variant. Returns
// http_response by value (DR-004) and takes the request by const
// reference. std::function is the chosen storage so users can pass any
// callable (lambda, function pointer, std::bind result, member-function
// adaptor) without leaking the concrete callable type into the route
// table.
//
// PRD-RSP-REQ-007 dispatch note: when the lookup_v2 path invokes this
// variant arm, the returned http_response prvalue is moved directly into
// modded_request::response via emplace(), matching the pointer-to-member
// dispatch path used by the v1 finalize_answer (see DR-004 §5.3).
using lambda_handler = std::function<::httpserver::http_response(const ::httpserver::http_request&)>;  // NOLINT(whitespace/line_length)

// route_entry: §4.7-shape value type stored per route in the route
// table. The `methods` mask holds every HTTP method this entry serves;
// the variant payload holds either a lambda (for on_* registrations)
// or a shared_ptr<http_resource> (for register_path / register_prefix
// registrations). `is_prefix` distinguishes prefix matching from exact
// matching at lookup time.
struct route_entry {
    method_set methods{};
    std::variant<lambda_handler,
                 std::shared_ptr<::httpserver::http_resource>> handler;
    bool is_prefix = false;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_ROUTE_ENTRY_HPP_
