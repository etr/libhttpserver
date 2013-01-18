/*
     This file is part of libhttpserver
     Copyright (C) 2011 Sebastiano Merlino

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
#include "http_resource.hpp"
#include "http_utils.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "webserver.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver 
{
//RESOURCE
void resource_init(map<string, bool>& allowed_methods)
{
    allowed_methods[MHD_HTTP_METHOD_GET] = true;
    allowed_methods[MHD_HTTP_METHOD_POST] = true;
    allowed_methods[MHD_HTTP_METHOD_PUT] = true;
    allowed_methods[MHD_HTTP_METHOD_HEAD] = true;
    allowed_methods[MHD_HTTP_METHOD_DELETE] = true;
    allowed_methods[MHD_HTTP_METHOD_TRACE] = true;
    allowed_methods[MHD_HTTP_METHOD_CONNECT] = true;
    allowed_methods[MHD_HTTP_METHOD_OPTIONS] = true;
}

};
