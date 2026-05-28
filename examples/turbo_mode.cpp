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

// turbo_mode.cpp - high-performance server configuration: turbo mode,
// suppressed Date header, fastopen queue and a tuned listen backlog.

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)
                                 .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
                                 .max_threads(4)
                                 .turbo()
                                 .suppress_date_header()
                                 .tcp_fastopen_queue_size(16)
                                 .listen_backlog(128)};

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, turbo world!");
    });

    ws.start(true);
    return 0;
}
