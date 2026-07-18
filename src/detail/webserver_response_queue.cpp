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

// Thin webserver_impl forwarder into the response_materializer behavior
// service (DR-014 §4.11). The materialise / decorate / queue / fallback
// logic moved to detail/response_materializer.cpp; only finalize_answer's
// materialize_and_queue_response call site remains external, so this single
// forwarder keeps it compiling unchanged during the migration (removed in
// the final slim step).

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <microhttpd.h>

namespace httpserver {
namespace detail {

MHD_Result webserver_impl::materialize_and_queue_response(
        MHD_Connection* connection,
        detail::modded_request* mr,
        http_resource* resource) {
    return response_mat_.materialize_and_queue_response(connection, mr,
                                                        resource);
}

}  // namespace detail
}  // namespace httpserver
