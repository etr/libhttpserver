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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 
     USA
*/

#include <cstdio>
#include <functional>
#include <iostream>
#include <sstream>
#include "http_utils.hpp"
#include "webserver.hpp"
#include "http_response.hpp"

using namespace std;

namespace httpserver
{

http_response::~http_response()
{
    if(ce != 0x0)
        webserver::unlock_cache_entry(ce);
}

size_t http_response::get_headers(std::map<std::string, std::string, header_comparator>& result)
{
    result = this->headers;
    return result.size();
}

size_t http_response::get_footers(std::map<std::string, std::string, header_comparator>& result)
{
    result = this->footers;
    return result.size();
}

size_t http_response::get_cookies(std::map<std::string, std::string, header_comparator>& result)
{
    result = this->cookies;
    return result.size();
}

//RESPONSE
shoutCAST_response::shoutCAST_response
(
    const std::string& content,
    int response_code,
    const std::string& content_type,
    bool autodelete
):
    http_response(
            this,
            content,
            response_code | http_utils::shoutcast_response,
            content_type,autodelete
    )
{
}

ssize_t deferred_response::cycle_callback(const std::string& buf)
{
    return -1;
}

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
    char* page = NULL;
    size_t size = http::load_file(filename.c_str(), &page);
    if(size)
        *response = MHD_create_response_from_buffer(
                size,
                page,
                MHD_RESPMEM_MUST_FREE
        );
    else
        *response = MHD_create_response_from_buffer(
                size,
                (void*) "",
                MHD_RESPMEM_PERSISTENT
        );
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
    int val = static_cast<deferred_response*>(cls)->cycle_callback(buf);
    if(val == -1)
        static_cast<deferred_response*>(cls)->completed = true;
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
        &long_polling_receive_response::data_generator, (void*) this, NULL);

    ws->register_to_topics(
            topics,
            connection_id,
            keepalive_secs,
            keepalive_msg
    );
}

ssize_t long_polling_receive_response::data_generator(
        void* cls,
        uint64_t pos,
        char* buf,
        size_t max
)
{
    long_polling_receive_response* _this = 
        static_cast<long_polling_receive_response*>(cls);

    if(_this->ws->pop_signaled(_this->connection_id))
    {
        string message;
        int size = _this->ws->read_message(_this->connection_id, message);
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

};
