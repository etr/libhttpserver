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
http_resource::http_resource() 
{
    this->allowed_methods[MHD_HTTP_METHOD_GET] = true;
    this->allowed_methods[MHD_HTTP_METHOD_POST] = true;
    this->allowed_methods[MHD_HTTP_METHOD_PUT] = true;
    this->allowed_methods[MHD_HTTP_METHOD_HEAD] = true;
    this->allowed_methods[MHD_HTTP_METHOD_DELETE] = true;
    this->allowed_methods[MHD_HTTP_METHOD_TRACE] = true;
    this->allowed_methods[MHD_HTTP_METHOD_CONNECT] = true;
    this->allowed_methods[MHD_HTTP_METHOD_OPTIONS] = true;
}

http_resource::~http_resource() 
{
}

http_response http_resource::render(const http_request& r) 
{
    if(this->is_allowed(r.get_method()))
    {
        return this->render_404();
    } 
    else 
    {
        return this->render_405();
    }
}

http_response http_resource::render_404() 
{
    return http_string_response(NOT_FOUND_ERROR, 404);
}

http_response http_resource::render_405() 
{
    return http_string_response(METHOD_ERROR, 405);
}

http_response http_resource::render_500() 
{
    return http_string_response(GENERIC_ERROR, 500);
}

http_response http_resource::render_GET(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::render_POST(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::render_PUT(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::render_DELETE(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::render_HEAD(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::render_TRACE(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::render_OPTIONS(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::render_CONNECT(const http_request& r) 
{
    return this->render(r);
}

http_response http_resource::route_request(const http_request& r) 
{
    string method = string_utilities::to_upper_copy(r.get_method());

    http_response res;

    if(method == MHD_HTTP_METHOD_GET) 
    {
        res = this->render_GET(r);
    } 
    else if (method == MHD_HTTP_METHOD_POST) 
    {
        res = this->render_POST(r);
    } 
    else if (method == MHD_HTTP_METHOD_PUT) 
    {
        res = this->render_PUT(r);
    } 
    else if (method == MHD_HTTP_METHOD_DELETE) 
    {
        res = this->render_DELETE(r);
    } 
    else if (method == MHD_HTTP_METHOD_HEAD) 
    {
        res = this->render_HEAD(r);
    } 
    else if (method == MHD_HTTP_METHOD_TRACE) 
    {
        res = this->render_TRACE(r);
    } 
    else if (method == MHD_HTTP_METHOD_OPTIONS) 
    {
        res = this->render_OPTIONS(r);
    } 
    else if (method == MHD_HTTP_METHOD_CONNECT) 
    {
        res = this->render_CONNECT(r);
    } 
    else 
    {
        res = this->render(r);
    }
    return res;
}

};
