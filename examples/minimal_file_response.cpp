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

// minimal_file_response.cpp - stream a file from disk as the response
// body using the http_response::file() factory.

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::file("test_content")
                   .with_header("Content-Type", "text/plain");
    });

    ws.start(true);
    return 0;
}
