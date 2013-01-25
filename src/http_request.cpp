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

#include "http_utils.hpp"
#include "http_request.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver
{

void http_request::set_method(const std::string& method)
{
    string_utilities::to_upper_copy(method, this->method);
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

size_t http_request::get_headers(std::map<std::string, std::string, header_comparator>& result) const
{
    result = this->headers;
    return result.size();
}

size_t http_request::get_footers(std::map<std::string, std::string, header_comparator>& result) const
{
    result = this->footers;
    return result.size();
}

size_t http_request::get_cookies(std::map<std::string, std::string, header_comparator>& result) const
{
    result = this->cookies;
    return result.size();
}

size_t http_request::get_args(std::map<std::string, std::string, arg_comparator>& result) const
{
    result = this->args;
    return result.size();
}

};
