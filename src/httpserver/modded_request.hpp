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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _MODDED_REQUEST_HPP_
#define _MODDED_REQUEST_HPP_

#include <string>

namespace httpserver
{

class webserver;
class http_request;
class http_request_builder;

struct modded_request
{
    struct MHD_PostProcessor *pp;
    std::string* complete_uri;
    std::string* st_url;
    webserver* ws;
    void(http_resource::*callback)(const http_request&, http_response**);
    http_request* dhr;
    http_response* dhrs;
    http_request_builder* request_builder;
    bool second;
};

};

#endif //_MODDED_REQUEST_HPP_
