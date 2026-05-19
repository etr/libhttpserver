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

// external_event_loop.cpp - run the daemon under EXTERNAL_SELECT and
// drive it via run_wait() from the application's own loop. The
// thread_safety(false) optimization is intentionally omitted for
// portability (some MHD builds reject the combination at start).

#include <csignal>
#include <iostream>

#include <httpserver.hpp>

static volatile bool running = true;

void signal_handler(int) {
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);

    httpserver::webserver ws{httpserver::create_webserver(8080)
                                 .start_method(httpserver::http::http_utils::EXTERNAL_SELECT)};

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello from external event loop!");
    });

    ws.start(false);

    std::cout << "Server running on port " << ws.get_bound_port() << std::endl;

    while (running) {
        ws.run_wait(1000);
        // Do other work between iterations as needed.
    }

    std::cout << "\nShutting down..." << std::endl;

    // Graceful shutdown: stop accepting new connections first.
    ws.quiesce();
    ws.stop();
    return 0;
}
