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

// Thin webserver_impl forwarders for the four gated hook-firing helpers
// into the hook_dispatcher behavior service (DR-014 §4.11). The real gating
// + context-construction + chain-sequencing logic moved to
// detail/hook_dispatcher.cpp. These keep the dispatch/materialize/completion
// call sites compiling unchanged during the migration; removed in the final
// slim step.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <microhttpd.h>

#include <memory>

namespace httpserver {
namespace detail {

bool webserver_impl::fire_before_handler_gated(
        detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    return hooks_dispatch_.fire_before_handler_gated(mr, hrm);
}

void webserver_impl::fire_after_handler_gated(detail::modded_request* mr,
                                              http_resource* resource) {
    hooks_dispatch_.fire_after_handler_gated(mr, resource);
}

void webserver_impl::fire_response_sent_gated(detail::modded_request* mr,
                                              http_resource* resource) {
    hooks_dispatch_.fire_response_sent_gated(mr, resource);
}

void webserver_impl::fire_request_completed_gated(
        detail::modded_request* mr,
        enum MHD_RequestTerminationCode toe) {
    hooks_dispatch_.fire_request_completed_gated(mr, toe);
}

}  // namespace detail
}  // namespace httpserver
