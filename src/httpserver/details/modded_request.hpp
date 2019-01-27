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

namespace httpserver
{

namespace details
{

struct modded_request
{
    struct MHD_PostProcessor *pp;
    std::string* complete_uri;
    std::string* standardized_url;
    webserver* ws;

    const std::shared_ptr<http_response> (httpserver::http_resource::*callback)(httpserver::http_request&);

    http_request* dhr;
    std::shared_ptr<http_response> dhrs;
    bool second;

    modded_request():
        pp(0x0),
        complete_uri(0x0),
        standardized_url(0x0),
        ws(0x0),
        dhr(0x0),
        second(false)
    {
    }

    modded_request(const modded_request& b):
        pp(b.pp),
        complete_uri(b.complete_uri),
        standardized_url(b.standardized_url),
        ws(b.ws),
        dhr(b.dhr),
        second(b.second)
    {
    }

    modded_request(modded_request&& b):
        pp(std::move(b.pp)),
        complete_uri(std::move(b.complete_uri)),
        standardized_url(std::move(b.standardized_url)),
        ws(std::move(b.ws)),
        dhr(std::move(b.dhr)),
        second(b.second)
    {
    }

    modded_request& operator=(const modded_request& b)
    {
        if (this == &b) return *this;

        this->pp = b.pp;
        this->complete_uri = b.complete_uri;
        this->standardized_url = b.standardized_url;
        this->ws = b.ws;
        this->dhr = b.dhr;
        this->second = b.second;

        return *this;
    }

    modded_request& operator=(modded_request&& b)
    {
        if (this == &b) return *this;

        this->pp = std::move(b.pp);
        this->complete_uri = std::move(b.complete_uri);
        this->standardized_url = std::move(b.standardized_url);
        this->ws = std::move(b.ws);
        this->dhr = std::move(b.dhr);
        this->second = b.second;

        return *this;
    }

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
