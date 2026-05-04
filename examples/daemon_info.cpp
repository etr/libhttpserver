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

#include <iostream>
#include <memory>

#include <httpserver.hpp>

class hello_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         return std::make_shared<httpserver::http_response>(httpserver::http_response::string("Hello, World!"));
     }
};

int main() {
    // Use port 0 to let the OS assign an ephemeral port
    httpserver::webserver ws = httpserver::create_webserver(0);

    hello_resource hr;
    ws.register_resource("/hello", &hr);
    ws.start(false);

    // Query daemon information
    std::cout << "libmicrohttpd version: "
              << httpserver::http::http_utils::get_mhd_version() << std::endl;
    std::cout << "Bound port: " << ws.get_bound_port() << std::endl;
    std::cout << "Listen FD: " << ws.get_listen_fd() << std::endl;
    std::cout << "Active connections: " << ws.get_active_connections() << std::endl;
    std::cout << "HTTP 200 reason: "
              << httpserver::http::http_utils::reason_phrase(200) << std::endl;
    std::cout << "HTTP 404 reason: "
              << httpserver::http::http_utils::reason_phrase(404) << std::endl;

    std::cout << "\nServer running on port " << ws.get_bound_port()
              << ". Press Ctrl+C to stop." << std::endl;

    // Block until interrupted
    ws.sweet_kill();

    return 0;
}
