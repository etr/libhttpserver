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

#include "http_utils.hpp"
#include "http_request.hpp"
#include "string_utilities.hpp"
#include <iostream>

using namespace std;

namespace httpserver
{

void http_request::set_method(const std::string& method)
{
    this->method = string_utilities::to_upper_copy(method);
}

bool http_request::check_digest_auth(
        const std::string& realm,
        const std::string& password,
        int nonce_timeout,
        bool& reload_nonce
) const
{
    int val = MHD_digest_auth_check(
            underlying_connection,
            realm.c_str(),
            digested_user.c_str(),
            password.c_str(),
            nonce_timeout
    );
    if(val == MHD_INVALID_NONCE)
    {
        reload_nonce = true;
        return false;
    }
    else if(val == MHD_NO)
    {
        reload_nonce = false;
        return false;
    }
    reload_nonce = false;
    return true;
}

const std::map<std::string, std::string, http::header_comparator> http_request::get_headers() const
{
    return this->headers;
}

const std::map<std::string, std::string, http::header_comparator> http_request::get_footers() const
{
    return this->footers;
}

const std::map<std::string, std::string, http::header_comparator> http_request::get_cookies() const
{
    return this->cookies;
}

const std::map<std::string, std::string, http::arg_comparator> http_request::get_args() const
{
    return this->args;
}

std::ostream &operator<< (std::ostream &os, const http_request &r)
{
    os << r.method << " Request [user:\"" << r.user << "\" pass:\"" << r.pass << "\"] path:\""
       << r.path << "\"" << std::endl;

    http::dump_header_map(os,"Headers",r.headers);
    http::dump_header_map(os,"Footers",r.footers);
    http::dump_header_map(os,"Cookies",r.cookies);
    http::dump_arg_map(os,"Query Args",r.args);

    os << "    Version [ " << r.version << " ] Requestor [ " << r.requestor
       << " ] Port [ " << r.requestor_port << " ]" << std::endl;

    return os;
}


}
