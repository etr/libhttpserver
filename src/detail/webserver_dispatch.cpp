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

// The dispatch behavior (resolve_resource_for_request,
// dispatch_resource_handler, finalize_answer, and the handler_exception
// helper) moved to the request_dispatcher behavior service
// (detail/request_dispatcher.cpp, DR-014 §4.11). Only serialize_allow_methods
// remains on webserver_impl: it is a thin test seam over
// detail::format_allow_header used by bench_warm_path.cpp and
// webserver_on_methods_test.cpp; production dispatch calls
// hrm->get_allow_header() directly.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <string>

#include "httpserver/detail/method_utils.hpp"

namespace httpserver {
namespace detail {

std::string webserver_impl::serialize_allow_methods(method_set allowed) const {
    return detail::format_allow_header(allowed);
}

}  // namespace detail
}  // namespace httpserver
