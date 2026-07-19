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

// request_dispatcher -- behavior service (DR-014, §4.11): the routing +
// auth + handler-invocation stage. finalize_answer orchestrates the
// websocket-upgrade probe, the pre-handler short-circuit path, route
// resolution, the route_resolved / before_handler / after_handler hook
// gates, handler dispatch (with the handler_exception chain), the 404 /
// 405 paths, and the hand-off to response materialisation.
//
// Holds references to the collaborators it drives: route_table (lookup),
// hook_dispatcher (all firing + gating), error_pages (404/405/500),
// response_materializer (queueing), websocket_upgrader (HAVE_WEBSOCKET),
// and const webserver_config (log_dispatch_error). Owns no state.
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "request_dispatcher.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_REQUEST_DISPATCHER_HPP_
#define SRC_HTTPSERVER_DETAIL_REQUEST_DISPATCHER_HPP_

#include <microhttpd.h>

#include <memory>
#include <optional>

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

struct webserver_config;
class http_resource;

namespace detail {

struct connection_context;
class route_table;
class hook_dispatcher;
class error_pages;
class response_materializer;
class websocket_upgrader;

class request_dispatcher {
 public:
    request_dispatcher(route_table& routes, hook_dispatcher& hooks,
                       error_pages& errors, response_materializer& materializer,
#ifdef HAVE_WEBSOCKET
                       websocket_upgrader& ws_upgrader,
#endif  // HAVE_WEBSOCKET
                       const webserver_config& config) noexcept
        : routes_(routes), hooks_(hooks), errors_(errors),
          materializer_(materializer),
#ifdef HAVE_WEBSOCKET
          ws_upgrader_(ws_upgrader),
#endif  // HAVE_WEBSOCKET
          config_(config) {}

    request_dispatcher(const request_dispatcher&) = delete;
    request_dispatcher& operator=(const request_dispatcher&) = delete;
    request_dispatcher(request_dispatcher&&) = delete;
    request_dispatcher& operator=(request_dispatcher&&) = delete;
    ~request_dispatcher() = default;

    // The finalize stage of the request (called by request_pipeline's
    // complete_request). Resolves the route, runs the hook gates + handler,
    // and queues the response. Returns the MHD queue result.
    MHD_Result finalize_answer(MHD_Connection* connection, connection_context* conn);

 private:
    // Websocket-upgrade probe. Forwards to ws_upgrader_ on HAVE_WEBSOCKET
    // builds; a no-op returning nullopt otherwise. Kept as a helper so the
    // HAVE_WEBSOCKET #ifdef stays out of finalize_answer's CCN count (lizard
    // counts a preprocessor conditional as a branch).
    std::optional<MHD_Result> try_ws_upgrade(MHD_Connection* connection,
                                             connection_context* conn);

    // Resolve the resource serving @p conn via routes_.lookup_v2 (cache ->
    // exact -> radix -> regex). Returns true and sets @p hrm on hit (also
    // replays captured params + populates the hook-ctx path template when a
    // route_resolved/before_handler hook is armed); false otherwise.
    bool resolve_resource_for_request(connection_context* conn,
                                      std::shared_ptr<http_resource>& hrm);

    // Invoke the resource handler bound to @p conn (pointer-to-member
    // dispatch), populating conn->response. On is_allowed=false, queues a 405
    // with an Allow header. On handler-throw, routes through the
    // handler_exception chain / safe internal-error path.
    void dispatch_resource_handler(connection_context* conn,
                                   const std::shared_ptr<http_resource>& hrm);

    route_table& routes_;
    hook_dispatcher& hooks_;
    error_pages& errors_;
    response_materializer& materializer_;
#ifdef HAVE_WEBSOCKET
    websocket_upgrader& ws_upgrader_;
#endif  // HAVE_WEBSOCKET
    const webserver_config& config_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_REQUEST_DISPATCHER_HPP_
