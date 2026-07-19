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

// response_materializer -- behavior service (DR-014, §4.11) that turns the
// staged http_response in conn->response into an MHD_Response, decorates it
// with headers/footers/cookies, queues it on the connection (via the plain
// or the RFC-7616 digest-challenge MHD path), fires the response_sent hook,
// and destroys the MHD handle. Owns the belt-and-suspenders fallback chain
// so a misbehaving user body/handler can never leave MHD with nothing to
// queue. A friend of http_response (reaches body_).
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "response_materializer.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_RESPONSE_MATERIALIZER_HPP_
#define SRC_HTTPSERVER_DETAIL_RESPONSE_MATERIALIZER_HPP_

#include <microhttpd.h>

#include <string>

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

struct webserver_config;
class http_response;
class http_resource;

namespace detail {

struct connection_context;
class error_pages;
class hook_dispatcher;

class response_materializer {
 public:
    response_materializer(error_pages& errors,
                          hook_dispatcher& hook_dispatch,
                          const std::string& digest_opaque,
                          const webserver_config& config) noexcept
        : errors_(errors), hook_dispatch_(hook_dispatch),
          digest_opaque_(digest_opaque), config_(config) {}

    response_materializer(const response_materializer&) = delete;
    response_materializer& operator=(const response_materializer&) = delete;
    response_materializer(response_materializer&&) = delete;
    response_materializer& operator=(response_materializer&&) = delete;
    ~response_materializer() = default;

    // Final stage of the request: materialise conn->response, decorate, queue,
    // fire response_sent, destroy the MHD handle. @p resource is the resolved
    // resource (nullptr when none) forwarded to the response_sent gate so it
    // reaches the per-route hook table without a weak_ptr lock().
    MHD_Result materialize_and_queue_response(MHD_Connection* connection,
                                              connection_context* conn,
                                              http_resource* resource);

 private:
    // Materialise conn->response into a raw MHD_Response, routing any
    // null/throw through the safe error paths (error_pages). Returns the raw
    // MHD handle, or nullptr only if every fallback also failed.
    struct MHD_Response* get_raw_response_with_fallback(connection_context* conn);

    // Kind-dispatched queueing: body_kind::digest_challenge goes through
    // MHD_queue_auth_required_response3 (RFC-7616), everything else through
    // MHD_queue_response. Returns the raw MHD status as int.
    int queue_response_dispatching_kind(MHD_Connection* connection,
                                        connection_context* conn,
                                        MHD_Response* raw_response);

    // Ask the response's body to produce a fresh headerless MHD_Response.
    static struct MHD_Response* materialize_response(http_response* resp);
    // Attach the response's header/footer/cookie maps to a materialised
    // MHD_Response.
    static void decorate_mhd_response(struct MHD_Response* response,
                                      const http_response& resp);

    error_pages& errors_;
    hook_dispatcher& hook_dispatch_;
    const std::string& digest_opaque_;
    const webserver_config& config_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_RESPONSE_MATERIALIZER_HPP_
