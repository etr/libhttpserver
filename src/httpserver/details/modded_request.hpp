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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _MODDED_REQUEST_HPP_
#define _MODDED_REQUEST_HPP_

#include "httpserver/http_request.hpp"

namespace httpserver
{

namespace details
{

struct modded_request
{
    struct MHD_PostProcessor *pp = 0x0;
    std::string* complete_uri = 0x0;
    std::string* standardized_url = 0x0;
    webserver* ws = 0x0;

    const std::shared_ptr<http_response> (httpserver::http_resource::*callback)(const httpserver::http_request&);

    http_request* dhr = 0x0;
    std::shared_ptr<http_response> dhrs;
    bool second = false;
    bool has_body = false;

    modded_request() = default;

    modded_request(const modded_request& b) = default;
    modded_request(modded_request&& b) = default;

    modded_request& operator=(const modded_request& b) = default;
    modded_request& operator=(modded_request&& b) = default;

    ~modded_request()
    {
        if (NULL != pp)
        {
            MHD_destroy_post_processor (pp);
        }
        if(second)
            delete dhr; //TODO: verify. It could be an error
        delete complete_uri;
        delete standardized_url;
    }

};

} //details

} //httpserver

#endif //_MODDED_REQUEST_HPP_
