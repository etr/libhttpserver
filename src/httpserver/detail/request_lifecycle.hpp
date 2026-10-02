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

// TASK-118 (plan D3/D4): the request dispatch pipeline of the native
// engine -- the v3 equivalent of v2's request_dispatcher. Runs one
// routed exchange through resolve, the lifecycle hook phases in their
// documented order, the v2 default error pages with Allow, and the
// request_completed tail. Defined in v3core
// (detail/request_lifecycle.cpp); NOT part of the installed surface.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/request_lifecycle.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_REQUEST_LIFECYCLE_HPP_
#define SRC_HTTPSERVER_DETAIL_REQUEST_LIFECYCLE_HPP_

#include <memory>

#include <httpserver/concurrency/task.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/hooks.hpp>
#include <httpserver/server/options.hpp>
#include <httpserver/server/routes.hpp>

namespace httpserver {

namespace detail {

class lifecycle_sink;

// The two construction-time custom error-page factories (plan D3),
// carried out of server_options at listen() by shared ownership. Null
// members mean the v2 default pages.
struct error_page_factories {
    using factory = server::server_options::response_factory;
    std::shared_ptr<const factory> not_found;
    std::shared_ptr<const factory> method_not_allowed;
};

// Dispatches one complete request head through the pipeline over @p x:
// request_received (short-circuit capable) -> resolve -> route_
// resolved (hit and miss alike) -> the miss/method-miss/hit branch
// (404/405 pages with Allow, before_handler consultation, the handler
// with its captures stamped as path args, handler_exception rescue) ->
// request_completed exactly once with the settle verdict. after_
// handler and response_sent fire through @p sink at response-head
// commit (see lifecycle_sink.hpp). A handler exception never crosses
// the boundary.
task<void> dispatch_request(const server::route_registry& routes,
                            const server::hook_bus& bus,
                            const error_page_factories& pages,
                            lifecycle_sink& sink, exchange& x);

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_REQUEST_LIFECYCLE_HPP_
