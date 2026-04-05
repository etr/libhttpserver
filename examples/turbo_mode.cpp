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

#include <memory>

#include <httpserver.hpp>

class hello_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         return std::make_shared<httpserver::string_response>("Hello, turbo world!");
     }
};

int main() {
    // Create a high-performance server with turbo mode,
    // suppressed date headers, and a thread pool.
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
        .max_threads(4)
        .turbo()
        .suppress_date_header()
        .tcp_fastopen_queue_size(16)
        .listen_backlog(128);

    hello_resource hr;
    ws.register_resource("/hello", &hr);
    ws.start(true);

    return 0;
}
