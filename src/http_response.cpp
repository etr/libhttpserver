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

const std::vector<std::pair<std::string, std::string> > http_response::get_headers()
{
    std::vector<std::pair<std::string, std::string> > to_ret;
    std::map<std::string, std::string, header_comparator>::const_iterator it;
    for(it = headers.begin(); it != headers.end(); it++)
#ifdef USE_CPP_ZEROX
        to_ret.push_back(std::make_pair((*it).first,(*it).second));
#else
        to_ret.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
    return to_ret;
}
size_t http_response::get_headers(std::vector<std::pair<std::string, std::string> >& result)
{
    std::map<std::string, std::string, header_comparator>::const_iterator it;
    for(it = headers.begin(); it != headers.end(); it++)
#ifdef USE_CPP_ZEROX
        result.push_back(std::make_pair((*it).first,(*it).second));
#else
        result.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
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
    for(it = footers.begin(); it != footers.end(); it++)
#ifdef USE_CPP_ZEROX
        to_ret.push_back(std::make_pair((*it).first,(*it).second));
#else
        to_ret.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
    return to_ret;
}
size_t http_response::get_footers(std::vector<std::pair<std::string, std::string> >& result)
{
    std::map<std::string, std::string, arg_comparator>::const_iterator it;
    for(it = footers.begin(); it != footers.end(); it++)
#ifdef USE_CPP_ZEROX
        result.push_back(std::make_pair((*it).first,(*it).second));
#else
        result.push_back(std::make_pair<std::string, std::string>((*it).first,(*it).second));
#endif
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

};
