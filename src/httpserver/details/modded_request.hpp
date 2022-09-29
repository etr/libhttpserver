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

#ifndef SRC_HTTPSERVER_DETAILS_MODDED_REQUEST_HPP_
#define SRC_HTTPSERVER_DETAILS_MODDED_REQUEST_HPP_

#include <string>
#include <memory>
#include <fstream>

#include "httpserver/http_request.hpp"

namespace httpserver {

namespace details {

struct modded_request {
    struct MHD_PostProcessor *pp = nullptr;
    std::string* complete_uri = nullptr;
    std::string* standardized_url = nullptr;
    webserver* ws = nullptr;

    std::shared_ptr<http_response> (httpserver::http_resource::*callback)(const httpserver::http_request&);

    http_request* dhr = nullptr;
    std::shared_ptr<http_response> dhrs;
    bool second = false;
    bool has_body = false;

    std::string upload_key;
    std::string upload_filename;
    std::ofstream* upload_ostrm = nullptr;

    modded_request() = default;

    modded_request(const modded_request& b) = default;
    modded_request(modded_request&& b) = default;

    modded_request& operator=(const modded_request& b) = default;
    modded_request& operator=(modded_request&& b) = default;

    ~modded_request() {
        if (nullptr != pp) {
            MHD_destroy_post_processor(pp);
        }
        if (second) {
            delete dhr;
        }
        delete complete_uri;
        delete standardized_url;
        delete upload_ostrm;
    }
};

}  // namespace details

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAILS_MODDED_REQUEST_HPP_
