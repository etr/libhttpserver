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

#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>

#include <httpserver.hpp>

static volatile bool running = true;

void signal_handler(int) {
    running = false;
}

class hello_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         return std::make_shared<httpserver::string_response>("Hello from external event loop!");
     }
};

int main() {
    signal(SIGINT, signal_handler);

    httpserver::webserver ws = httpserver::create_webserver(8080)
        .start_method(httpserver::http::http_utils::EXTERNAL_SELECT)
        .no_thread_safety();

    hello_resource hr;
    ws.register_resource("/hello", &hr);
    ws.start(false);

    std::cout << "Server running on port " << ws.get_bound_port() << std::endl;

    // Drive the event loop externally using run_wait
    while (running) {
        // Block for up to 1000ms waiting for HTTP activity
        ws.run_wait(1000);

        // You can do other work here between iterations
    }

    std::cout << "\nShutting down..." << std::endl;

    // Graceful shutdown: stop accepting new connections first
    ws.quiesce();
    ws.stop();

    return 0;
}
