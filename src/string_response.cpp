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

#include "httpserver/string_response.hpp"

#include <microhttpd.h>
#include <stddef.h>

using namespace std;

namespace httpserver
{

MHD_Response* string_response::get_raw_response()
{
    size_t size = &(*content.end()) - &(*content.begin());
    return MHD_create_response_from_buffer(
            size,
            (void*) content.c_str(),
            MHD_RESPMEM_PERSISTENT
    );
}

}
