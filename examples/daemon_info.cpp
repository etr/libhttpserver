/*
     This file is part of libhttpserver
     Copyright (C) 2011-2025 Sebastiano Merlino

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

// daemon_info.cpp - introspect the running daemon (bound port, listen FD,
// active connections, etc.) after start(false). Port 0 lets the OS pick
// an ephemeral port so multiple instances can coexist.

#include <iostream>

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(0)};

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, World!");
    });

    ws.start(false);

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

    ws.stop_and_wait();
    return 0;
}
