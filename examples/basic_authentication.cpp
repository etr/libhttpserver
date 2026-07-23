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
// SECURITY NOTE: Credentials MUST NOT be hardcoded in source code (CWE-798).
// This example loads BASIC_USER and BASIC_PASS from environment variables.
// Never reflect the password in the response body.
//
// Usage:
//   export BASIC_USER=myuser BASIC_PASS=mysecretpassword
//   ./basic_authentication
//   curl -u myuser:mysecretpassword http://localhost:8080/hello

#include <cstdlib>
#include <iostream>
#include <string>

#include <httpserver.hpp>

int main() {
    const char* expected_user = std::getenv("BASIC_USER");
    const char* expected_pass = std::getenv("BASIC_PASS");
    if (!expected_user || !expected_pass) {
        std::cerr << "basic_authentication: BASIC_USER and BASIC_PASS "
                     "environment variables must be set.\n";
        return 1;
    }

    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/hello", [expected_user, expected_pass]
                        (const httpserver::http_request& req) {
        if (req.get_user() != expected_user || req.get_pass() != expected_pass) {
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
