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
#include "http_utils.hpp"
#include "webserver.hpp"
#include "http_response.hpp"

#include <iostream>

using namespace std;

namespace httpserver
{

struct data_closure
{
    webserver* ws;
    string connection_id;
    string topic;
};

const std::vector<std::pair<std::string, std::string> > http_response::get_headers()
{
    std::vector<std::pair<std::string, std::string> > to_ret;
    std::map<std::string, std::string, header_comparator>::const_iterator it;
    for(it = headers.begin(); it != headers.end(); ++it)
        to_ret.push_back(*it);
    return to_ret;
}
size_t http_response::get_headers(std::vector<std::pair<std::string, std::string> >& result)
{
    std::map<std::string, std::string, header_comparator>::const_iterator it;
    for(it = headers.begin(); it != headers.end(); ++it)
        result.push_back(*it);
    return result.size();
}
size_t http_response::get_headers(std::map<std::string, std::string, header_comparator>& result)
{
    result = this->headers;
    return result.size();
}

const std::vector<std::pair<std::string, std::string> > http_response::get_footers()
{
    std::vector<std::pair<std::string, std::string> > to_ret;
    std::map<std::string, std::string, header_comparator>::const_iterator it;
    for(it = footers.begin(); it != footers.end(); ++it)
        to_ret.push_back(*it);
    return to_ret;
}
size_t http_response::get_footers(std::vector<std::pair<std::string, std::string> >& result)
{
    std::map<std::string, std::string, arg_comparator>::const_iterator it;
    for(it = footers.begin(); it != footers.end(); ++it)
        result.push_back(*it);
    return result.size();
}
size_t http_response::get_footers(std::map<std::string, std::string, header_comparator>& result)
{
    result = this->footers;
    return result.size();
}

//RESPONSE
shoutCAST_response::shoutCAST_response
(
    const std::string& content,
    int response_code,
    const std::string& content_type
):
    http_response(http_response::SHOUTCAST_CONTENT, content, response_code | http_utils::shoutcast_response, content_type)
{
}

void http_response::get_raw_response(MHD_Response** response, bool* found, webserver* ws)
{
    size_t size = &(*content.end()) - &(*content.begin());
    *response = MHD_create_response_from_buffer(size, (void*) content.c_str(), MHD_RESPMEM_PERSISTENT);
}

void http_file_response::get_raw_response(MHD_Response** response, bool* found, webserver* ws)
{
    char* page = NULL;
    size_t size = http::load_file(filename.c_str(), &page);
    if(size)
        *response = MHD_create_response_from_buffer(size, page, MHD_RESPMEM_MUST_FREE);
    else
        *found = false;
}

void long_polling_receive_response::get_raw_response(MHD_Response** response, bool* found, webserver* ws)
{
    data_closure* dc = new data_closure();
    dc->ws = ws;
    dc->topic = topic;
    generate_random_uuid(dc->connection_id);
    *response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, 80,
            &long_polling_receive_response::data_generator, (void*) dc, &long_polling_receive_response::free_callback);
    ws->register_to_topic(dc->topic, dc->connection_id);
}

ssize_t long_polling_receive_response::data_generator (void* cls, uint64_t pos, char* buf, size_t max)
{
    data_closure* dc = static_cast<data_closure*>(cls);
    if(dc->ws->pop_signaled(dc->connection_id))
    {
        string message;
        int size = dc->ws->read_message_from_topic(dc->topic, message);
        memcpy(buf, message.c_str(), size);
        return size;
    }
    else
        return 0;
}

void long_polling_receive_response::free_callback(void* cls)
{
    delete static_cast<data_closure*>(cls);
}

void long_polling_send_response::get_raw_response(MHD_Response** response, bool* found, webserver* ws)
{
    http_response::get_raw_response(response, found, ws);
    ws->send_message_to_topic(topic, content);
}

void clone_response(const http_response& hr, http_response** dhrs)
{
    switch(hr.response_type)
    {
        case(http_response::STRING_CONTENT):
            *dhrs = new http_string_response(hr);
            return;
        case(http_response::FILE_CONTENT):
            *dhrs = new http_file_response(hr);
            return;
        case(http_response::SHOUTCAST_CONTENT):
            *dhrs = new shoutCAST_response(hr);
            return;
        case(http_response::DIGEST_AUTH_FAIL):
            *dhrs = new http_digest_auth_fail_response(hr);
            return;
        case(http_response::BASIC_AUTH_FAIL):
            *dhrs = new http_basic_auth_fail_response(hr);
            return;
        case(http_response::SWITCH_PROTOCOL):
            *dhrs = new switch_protocol_response(hr);
            return;
        case(http_response::LONG_POLLING_RECEIVE):
            *dhrs = new long_polling_receive_response(hr);
            return;
        case(http_response::LONG_POLLING_SEND):
            *dhrs = new long_polling_send_response(hr);
            return;
    }
}

};
