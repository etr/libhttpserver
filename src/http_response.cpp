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

#include <cstdio>
#include <functional>
#include <iostream>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include "httpserver/http_utils.hpp"
#include "httpserver/webserver.hpp"
#include "httpserver/http_response.hpp"

using namespace std;

namespace httpserver
{

MHD_Response* http_response::get_raw_response()
{
    return MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
}

void http_response::decorate_response(MHD_Response* response)
{
    map<string, string, http::header_comparator>::iterator it;

    for (it=headers.begin() ; it != headers.end(); ++it)
        MHD_add_response_header(
                response,
                (*it).first.c_str(),
                (*it).second.c_str()
        );

    for (it=footers.begin() ; it != footers.end(); ++it)
        MHD_add_response_footer(response,
                (*it).first.c_str(),
                (*it).second.c_str()
        );

    for (it=cookies.begin(); it != cookies.end(); ++it)
        MHD_add_response_header(
                response,
                "Set-Cookie",
                ((*it).first + "=" + (*it).second).c_str()
        );
}

int http_response::enqueue_response(MHD_Connection* connection, MHD_Response* response)
{
    return MHD_queue_response(connection, response_code, response);
}

void http_response::shoutCAST()
{
    response_code |= http::http_utils::shoutcast_response;
}

std::ostream &operator<< (std::ostream &os, const http_response &r)
{
    os << "Response [response_code:" << r.response_code << "]" << std::endl;

    http::dump_header_map(os,"Headers",r.headers);
    http::dump_header_map(os,"Footers",r.footers);
    http::dump_header_map(os,"Cookies",r.cookies);

    return os;
}

}
