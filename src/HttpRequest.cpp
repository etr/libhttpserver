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
#include "HttpUtils.hpp"
#include "HttpRequest.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver
{
//REQUEST
void HttpRequest::setMethod(const std::string& method)
{
    this->method = string_utilities::to_upper_copy(method);
}

bool HttpRequest::checkDigestAuth(const std::string& realm, const std::string& password, int nonce_timeout, bool& reloadNonce) const
{
    int val = MHD_digest_auth_check(underlying_connection, realm.c_str(), digestedUser.c_str(), password.c_str(), nonce_timeout);
    if(val == MHD_INVALID_NONCE)
    {
        reloadNonce = true;
        return false;
    }
    else if(val == MHD_NO)
    {
        reloadNonce = false;
        return false;
    }
    reloadNonce = false;
    return true;
}

};
