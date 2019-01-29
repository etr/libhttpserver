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

#include "deferred_response.hpp"

using namespace std;

namespace httpserver
{

namespace details
{

ssize_t cb(void* cls, uint64_t pos, char* buf, size_t max)
{
    ssize_t val = static_cast<deferred_response*>(cls)->cycle_callback(
                  static_cast<deferred_response*>(cls)->priv_data, buf, max);
    if(val == -1)
    {
        static_cast<deferred_response*>(cls)->completed = true;
    }

    return val;
}

}

MHD_Response* deferred_response::get_raw_response()
{
    if(!completed)
    {
        return MHD_create_response_from_callback(
                MHD_SIZE_UNKNOWN,
                1024,
                &details::cb,
                this,
                NULL
        );
    }
    else
    {
        return static_cast<string_response*>(this)->get_raw_response();
    }
}

void deferred_response::decorate_response(MHD_Response* response)
{
    if(completed)
    {
        static_cast<string_response*>(this)->decorate_response(response);
    }
}

}

