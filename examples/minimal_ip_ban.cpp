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

#include <memory>

#include <httpserver.hpp>

class hello_world_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render(const httpserver::http_request&) {
         return std::shared_ptr<httpserver::http_response>(new httpserver::http_response(httpserver::http_response::string("Hello, World!")));
     }
};

// TASK-029: The v2.0 public IP-control API is the pair block_ip / unblock_ip,
// usable under the default ACCEPT policy. The historical allow_ip /
// disallow_ip pair (under REJECT) was dropped from the public surface.
int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.block_ip("10.0.0.1");

    auto hwr = std::make_shared<hello_world_resource>();
    ws.register_path("/hello", hwr);
    ws.start(true);

    return 0;
}
