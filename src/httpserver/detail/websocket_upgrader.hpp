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

// websocket_upgrader -- behavior service (DR-014, §4.11) owning the RFC-6455
// upgrade handshake and the per-connection frame receive loop. Holds the
// ws_registry (handler lookup) and nothing else. Entirely HAVE_WEBSOCKET-
// gated, like the ws_registry member it depends on. The webserver_impl
// try_handle_websocket_upgrade method forwards here (with a no-op on
// HAVE_WEBSOCKET-off builds).
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "websocket_upgrader.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_WEBSOCKET_UPGRADER_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSOCKET_UPGRADER_HPP_

#include <microhttpd.h>
#ifdef HAVE_WEBSOCKET
#include <microhttpd_ws.h>
#endif  // HAVE_WEBSOCKET

#include <cstddef>
#include <memory>
#include <optional>

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

class websocket_handler;

namespace detail {

struct connection_context;
class ws_registry;

#ifdef HAVE_WEBSOCKET
class websocket_upgrader {
 public:
    explicit websocket_upgrader(ws_registry& ws) noexcept : ws_(ws) {}

    websocket_upgrader(const websocket_upgrader&) = delete;
    websocket_upgrader& operator=(const websocket_upgrader&) = delete;
    websocket_upgrader(websocket_upgrader&&) = delete;
    websocket_upgrader& operator=(websocket_upgrader&&) = delete;
    ~websocket_upgrader() = default;

    // Probe the request for an RFC-6455 upgrade. Returns std::nullopt when
    // this is not a websocket upgrade (caller falls through to normal HTTP
    // dispatch) or when a handshake header is missing/malformed but a 400 was
    // queued; an engaged optional carries the MHD_queue_response result.
    std::optional<MHD_Result> try_handle(MHD_Connection* connection,
                                         connection_context* conn);

 private:
    // Closure handed to MHD_create_response_for_upgrade: keeps the resolved
    // handler alive across the upgrade callback even if a concurrent
    // unregister_ws_resource drops the registration.
    struct ws_upgrade_data {
        std::shared_ptr<websocket_handler> handler;
    };

    // Validate the RFC-6455 handshake headers; returns the Sec-WebSocket-Key
    // on success, std::nullopt if any required header is missing/malformed.
    std::optional<const char*> validate_websocket_handshake(
        MHD_Connection* connection);

    // Resolve the handler for conn->standardized_url and queue the 101 switch.
    // std::nullopt on no-handler / MHD failure / accept-header failure -- all
    // degraded to normal HTTP dispatch by the caller.
    std::optional<MHD_Result> complete_websocket_upgrade(
        MHD_Connection* connection, connection_context* conn, const char* ws_key);

    // MHD upgrade callback (cls = ws_upgrade_data*). Owns the frame loop.
    static void upgrade_handler(void* cls, struct MHD_Connection* connection,
                                void* req_cls, const char* extra_in,
                                size_t extra_in_size, MHD_socket sock,
                                struct MHD_UpgradeResponseHandle* urh);

    ws_registry& ws_;
};
#endif  // HAVE_WEBSOCKET

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_WEBSOCKET_UPGRADER_HPP_
