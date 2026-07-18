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

// Thin webserver_impl forwarder for the websocket upgrade probe into the
// websocket_upgrader behavior service (DR-014 §4.11). The handshake /
// completion / frame-loop logic moved to detail/websocket_upgrader.cpp.
// This forwarder lives outside the HAVE_WEBSOCKET guard because
// finalize_answer (webserver_request.cpp) calls it unconditionally; on
// HAVE_WEBSOCKET-off builds it is a no-op that degrades to normal HTTP
// dispatch. Removed in the final slim step.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <microhttpd.h>

#include <optional>

namespace httpserver {
namespace detail {

std::optional<MHD_Result>
webserver_impl::try_handle_websocket_upgrade(MHD_Connection* connection,
                                             detail::modded_request* mr) {
#ifdef HAVE_WEBSOCKET
    return ws_upgrader_.try_handle(connection, mr);
#else
    (void)connection;
    (void)mr;
    return std::nullopt;
#endif  // HAVE_WEBSOCKET
}

}  // namespace detail
}  // namespace httpserver
