/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "httpserver/http_resource.hpp"
#include <microhttpd.h>
#include <iosfwd>
#include "httpserver/string_response.hpp"

namespace httpserver { class http_response; }

namespace httpserver {

// RESOURCE
void resource_init(std::map<std::string, bool>* method_state) {
    (*method_state)[MHD_HTTP_METHOD_GET] = true;
    (*method_state)[MHD_HTTP_METHOD_POST] = true;
    (*method_state)[MHD_HTTP_METHOD_PUT] = true;
    (*method_state)[MHD_HTTP_METHOD_HEAD] = true;
    (*method_state)[MHD_HTTP_METHOD_DELETE] = true;
    (*method_state)[MHD_HTTP_METHOD_TRACE] = true;
    (*method_state)[MHD_HTTP_METHOD_CONNECT] = true;
    (*method_state)[MHD_HTTP_METHOD_OPTIONS] = true;
    (*method_state)[MHD_HTTP_METHOD_PATCH] = true;
}

namespace details {

std::shared_ptr<http_response> empty_render(const http_request&) {
    return std::shared_ptr<http_response>(new string_response());
}

}  // namespace details

}  // namespace httpserver
