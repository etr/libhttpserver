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

// iovec_response_example.cpp - build a response body from multiple
// borrowed buffers without copying. The buffers must outlive the
// response; static-lifetime literals satisfy that contract trivially.

#include <vector>

#include <httpserver.hpp>

static const char kPart1[] = "{\"header\": \"value\", ";
static const char kPart2[] = "\"items\": [1, 2, 3], ";
static const char kPart3[] = "\"footer\": \"end\"}";

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/data", [](const httpserver::http_request&) {
        std::vector<httpserver::iovec_entry> parts = {
            { kPart1, sizeof(kPart1) - 1 },
            { kPart2, sizeof(kPart2) - 1 },
            { kPart3, sizeof(kPart3) - 1 },
        };
        return httpserver::http_response::iovec(parts)
                   .with_header("Content-Type", "application/json");
    });

    ws.start(true);
    return 0;
}
