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
#include <cstdio>
#include "HttpUtils.hpp"
#include "Webserver.hpp"
#include "HttpResponse.hpp"

#include <iostream>

using namespace std;

namespace httpserver
{
//RESPONSE
HttpFileResponse::HttpFileResponse
(
    const string& filename,
    int responseCode,
    const std::string& contentType
)
{
    FILE* f;
    this->filename = filename;
    if(!(f = fopen(filename.c_str(), "r")))
    {
        this->responseType = HttpResponse::STRING_CONTENT;
        this->content = NOT_FOUND_ERROR;
        this->responseCode = HttpUtils::http_not_found;
        this->setHeader(HttpUtils::http_header_content_type, contentType);
        this->fp = -1;
    }
    else
    {
        this->responseType = HttpResponse::FILE_CONTENT;
        this->responseCode = responseCode;
        this->fp = fileno(f);
    }
}

ShoutCASTResponse::ShoutCASTResponse
(
    const std::string& content,
    int responseCode,
    const std::string& contentType
):
    HttpResponse(HttpResponse::SHOUTCAST_CONTENT, content, responseCode | HttpUtils::shoutcast_response, contentType)
{
}

};
