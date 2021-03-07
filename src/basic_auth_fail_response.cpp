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

#include "httpserver/basic_auth_fail_response.hpp"
#include <microhttpd.h>
#include <iosfwd>

struct MHD_Connection;
struct MHD_Response;

namespace httpserver {

int basic_auth_fail_response::enqueue_response(MHD_Connection* connection, MHD_Response* response) {
    return MHD_queue_basic_auth_fail_response(connection, realm.c_str(), response);
}

}  // namespace httpserver
