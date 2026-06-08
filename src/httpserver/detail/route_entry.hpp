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

#include <memory>

#include "httpserver/http_method.hpp"

namespace httpserver {
class http_resource;
}  // namespace httpserver

namespace httpserver {
namespace detail {

// route_entry: §4.7-shape value type stored per route in the route
// table. The `methods` mask holds every HTTP method this entry serves;
// the `handler` payload is a shared_ptr to the http_resource serving
// the route — either a class-derived resource (for register_path /
// register_prefix registrations) or a lambda_resource shim
// (for on_* / route registrations, which wrap the user lambda in a
// lambda_resource owning one slot per HTTP method). `is_prefix`
// distinguishes prefix matching from exact matching at lookup time.
//
// TASK-071 history: this field was originally a
// `std::variant<lambda_handler, shared_ptr<http_resource>>`. The
// lambda_handler arm was never populated — every writer wrapped user
// lambdas in lambda_resource (see webserver_routes.cpp's
// prepare_or_create_lambda_shim and the lambda_resource header doc-
// comment for why the shim composes cleanly with register_path's
// resource storage). The variant arm has been removed; the
// lambda_handler typedef now lives in detail/lambda_resource.hpp
// where its remaining uses are (the per-method slot signature).
struct route_entry {
    method_set methods{};
    std::shared_ptr<::httpserver::http_resource> handler;
    bool is_prefix = false;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_ROUTE_ENTRY_HPP_
