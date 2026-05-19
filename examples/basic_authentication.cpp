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

// basic_authentication.cpp - per-request HTTP Basic auth check inside a
// lambda handler. For centralized auth that intercepts every request,
// see centralized_authentication.cpp.
//
// NOTE: Credentials are hardcoded here for illustration only. In production,
// load expected values from environment variables or a secrets store — never
// from source code. Never reflect the password in the response body.

#include <string>

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/hello", [](const httpserver::http_request& req) {
        if (req.get_user() != "myuser" || req.get_pass() != "mypass") {
            return httpserver::http_response::unauthorized(
                "Basic", "test@example.com", "FAIL");
        }
        // Only echo the username — never reflect the password back to the client.
        return httpserver::http_response::string(
            "Hello, " + std::string(req.get_user()) + "!");
    });

    ws.start(true);
    return 0;
}
