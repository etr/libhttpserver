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
#ifdef DEBUG
    std::map<std::string, bool>::iterator it;
    for(it = allowed_methods.begin(); it != allowed_methods.end(); ++it)
    {
        std::cout << (*it).first << " -> " << (*it).second << std::endl;
    }
#endif //DEBUG

}

http_resource::~http_resource() 
{
}

http_response http_resource::render(const http_request& r) 
{
    return http_string_response("", http_utils::http_ok);
}

void http_resource::render(const http_request& r, http_response** res)
{
    route_request(r, res);
}

http_response http_resource::route_request(const http_request& r)
{
    string method;
    r.get_method(method);
    string_utilities::to_upper(method);
    if(method == MHD_HTTP_METHOD_GET) 
        return this->render_GET(r);
    else if (method == MHD_HTTP_METHOD_POST) 
        return this->render_POST(r);
    else if (method == MHD_HTTP_METHOD_PUT) 
        return this->render_PUT(r);
    else if (method == MHD_HTTP_METHOD_DELETE) 
        return this->render_DELETE(r);
    else if (method == MHD_HTTP_METHOD_HEAD) 
        return this->render_HEAD(r);
    else if (method == MHD_HTTP_METHOD_TRACE) 
        return this->render_TRACE(r);
    else if (method == MHD_HTTP_METHOD_OPTIONS) 
        return this->render_OPTIONS(r);
    else if (method == MHD_HTTP_METHOD_CONNECT) 
        return this->render_CONNECT(r);
    else
        return this->render(r);
}

void http_resource::route_request(const http_request& r, http_response** res)
{
    http_response hr(this->route_request(r));
    clone_response(hr, res);
}

http_response http_resource::render_GET(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_GET(const http_request& r, http_response** res) 
{
    render(r, res);
}

http_response http_resource::render_POST(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_POST(const http_request& r, http_response** res) 
{
    render(r, res);
}

http_response http_resource::render_PUT(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_PUT(const http_request& r, http_response** res) 
{
    render(r, res);
}

http_response http_resource::render_DELETE(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_DELETE(const http_request& r, http_response** res) 
{
    render(r, res);
}

http_response http_resource::render_HEAD(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_HEAD(const http_request& r, http_response** res) 
{
    render(r, res);
}

http_response http_resource::render_TRACE(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_TRACE(const http_request& r, http_response** res) 
{
    render(r, res);
}

http_response http_resource::render_OPTIONS(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_OPTIONS(const http_request& r, http_response** res) 
{
    render(r, res);
}

http_response http_resource::render_CONNECT(const http_request& r) 
{
    return this->render(r);
}

void http_resource::render_CONNECT(const http_request& r, http_response** res) 
{
    render(r, res);
}

};
