/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

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
#include "http_utils.hpp"
#include "details/http_resource_mirror.hpp"
#include "details/event_tuple.hpp"
#include "webserver.hpp"
#include "http_response.hpp"
#include "http_response_builder.hpp"

using namespace std;

namespace httpserver
{

http_response::http_response(const http_response_builder& builder):
    content(builder._content_hook),
    response_code(builder._response_code),
    autodelete(builder._autodelete),
    realm(builder._realm),
    opaque(builder._opaque),
    reload_nonce(builder._reload_nonce),
    fp(-1),
    filename(builder._content_hook),
    headers(builder._headers),
    footers(builder._footers),
    cookies(builder._cookies),
    topics(builder._topics),
    keepalive_secs(builder._keepalive_secs),
    keepalive_msg(builder._keepalive_msg),
    send_topic(builder._send_topic),
    underlying_connection(0x0),
    ca(0x0),
    closure_data(0x0),
    ce(builder._ce),
    cycle_callback(builder._cycle_callback),
    get_raw_response(this, builder._get_raw_response),
    decorate_response(this, builder._decorate_response),
    enqueue_response(this, builder._enqueue_response),
    completed(false),
    ws(0x0),
    connection_id(0x0)
{
}

http_response::~http_response()
{
    if(ce != 0x0)
        webserver::unlock_cache_entry(ce);
}

size_t http_response::get_headers(std::map<std::string, std::string, header_comparator>& result) const
{
    result = this->headers;
    return result.size();
}

size_t http_response::get_footers(std::map<std::string, std::string, header_comparator>& result) const
{
    result = this->footers;
    return result.size();
}

size_t http_response::get_cookies(std::map<std::string, std::string, header_comparator>& result) const
{
    result = this->cookies;
    return result.size();
}

//RESPONSE
void http_response::get_raw_response_str(MHD_Response** response, webserver* ws)
{
    size_t size = &(*content.end()) - &(*content.begin());
    *response = MHD_create_response_from_buffer(
            size,
            (void*) content.c_str(),
            MHD_RESPMEM_PERSISTENT
    );
}

void http_response::decorate_response_str(MHD_Response* response)
{
    map<string, string, header_comparator>::iterator it;

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

int http_response::enqueue_response_str(
        MHD_Connection* connection,
        MHD_Response* response
)
{
    return MHD_queue_response(connection, response_code, response);
}

void http_response::decorate_response_cache(MHD_Response* response)
{
}

int http_response::enqueue_response_basic(
        MHD_Connection* connection,
        MHD_Response* response
)
{
    return MHD_queue_basic_auth_fail_response(
            connection,
            realm.c_str(),
            response
    );
}

int http_response::enqueue_response_digest(
        MHD_Connection* connection,
        MHD_Response* response
)
{
    return MHD_queue_auth_fail_response(
            connection,
            realm.c_str(),
            opaque.c_str(),
            response,
            reload_nonce ? MHD_YES : MHD_NO
    );
}

void http_response::get_raw_response_file(
        MHD_Response** response,
        webserver* ws
)
{
    int fd = open(filename.c_str(), O_RDONLY);
    size_t size = lseek(fd, 0, SEEK_END);
    if(size)
    {
        *response = MHD_create_response_from_fd(size, fd);
    }
    else
    {
        *response = MHD_create_response_from_buffer(
                0,
                (void*) "",
                MHD_RESPMEM_PERSISTENT
        );
    }
}

void http_response::get_raw_response_cache(
        MHD_Response** response,
        webserver* ws
)
{
    bool valid;
    http_response* r;
    if(ce == 0x0)
        r = ws->get_from_cache(content, &valid, &ce, true, false);
    else
        webserver::get_response(ce, &r);
    r->get_raw_response(response, ws);
    r->decorate_response(*response); //It is done here to avoid to search two times for the same element

    //TODO: Check if element is not in cache and throw exception
}

namespace details
{

ssize_t cb(void* cls, uint64_t pos, char* buf, size_t max)
{
    ssize_t val = static_cast<http_response*>(cls)->cycle_callback(buf, max);
    if(val == -1)
        static_cast<http_response*>(cls)->completed = true;
    return val;
}

}

void http_response::get_raw_response_deferred(
        MHD_Response** response,
        webserver* ws
)
{
    if(!completed)
        *response = MHD_create_response_from_callback(
                MHD_SIZE_UNKNOWN,
                1024,
                &details::cb,
                this,
                NULL
        );
    else
        static_cast<http_response*>(this)->get_raw_response(response, ws);
}

void http_response::decorate_response_deferred(MHD_Response* response)
{
    if(completed)
        static_cast<http_response*>(this)->decorate_response(response);
}

void http_response::get_raw_response_lp_receive(
        MHD_Response** response,
        webserver* ws
)
{
    this->ws = ws;
    this->connection_id = MHD_get_connection_info(
            this->underlying_connection,
            MHD_CONNECTION_INFO_CLIENT_ADDRESS
    )->client_addr;

    *response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, 80,
        &http_response::data_generator, (void*) this, NULL);

    ws->register_to_topics(
            topics,
            connection_id,
            keepalive_secs,
            keepalive_msg
    );
}

ssize_t http_response::data_generator(
        void* cls,
        uint64_t pos,
        char* buf,
        size_t max
)
{
    http_response* _this = static_cast<http_response*>(cls);

    if(_this->ws->pop_signaled(_this->connection_id))
    {
        string message;
        size_t size = _this->ws->read_message(_this->connection_id, message);
        memcpy(buf, message.c_str(), size);
        return size;
    }
    else
        return 0;
}

void http_response::get_raw_response_lp_send(
        MHD_Response** response,
        webserver* ws
)
{
    http_response::get_raw_response_str(response, ws);
    ws->send_message_to_topic(send_topic, content);
}

std::ostream &operator<< (std::ostream &os, const http_response &r)
{
    os << "Response [response_code:" << r.response_code << "]" << std::endl;

    http::dump_header_map(os,"Headers",r.headers);
    http::dump_header_map(os,"Footers",r.footers);
    http::dump_header_map(os,"Cookies",r.cookies);

    return os;
}

    
};
