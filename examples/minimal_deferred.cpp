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

// minimal_deferred.cpp - serve a response body in chunks using a deferred
// callback lambda. The lambda is called repeatedly by libmicrohttpd until it
// returns -1 (signalling end-of-body). The `done` flag ensures the body is
// sent exactly once; the second call returns -1 to close the stream.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::deferred(
            [body = std::string("hello from deferred"),
             done = false](std::uint64_t, char* buf,
                           std::size_t max) mutable -> ssize_t {
                if (done) return -1;
                done = true;
                std::size_t n = std::min(body.size(), max);
                std::memcpy(buf, body.data(), n);
                return static_cast<ssize_t>(n);
            });
    });

    ws.start(true);
    return 0;
}
