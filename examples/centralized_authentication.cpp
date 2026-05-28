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

// centralized_authentication.cpp - install a server-wide auth_handler
// that runs before every request. The handler returns nullptr to
// accept, or an http_response to reject. auth_skip_paths lists routes
// that bypass auth entirely.
//
// SECURITY NOTE: Credentials MUST NOT be hardcoded in source code (CWE-798).
// This example loads AUTH_USER and AUTH_PASS from environment variables.
//
// Usage:
//   # Set credentials and start the server
//   export AUTH_USER=myuser AUTH_PASS=mysecretpassword
//   ./centralized_authentication
//
//   # Without auth - should get 401 Unauthorized
//   curl -v http://localhost:8080/api
//
//   # With valid auth - should get 200 OK
//   curl -u myuser:mysecretpassword http://localhost:8080/api
//
//   # Health endpoint (skip path) - works without auth
//   curl http://localhost:8080/health

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <httpserver.hpp>

// Returns nullptr to allow the request, or an http_response to reject it.
std::shared_ptr<httpserver::http_response> auth_handler(
        const httpserver::http_request& req) {
    const char* expected_user = std::getenv("AUTH_USER");
    const char* expected_pass = std::getenv("AUTH_PASS");
    if (!expected_user || !expected_pass) {
        std::cerr << "centralized_authentication: AUTH_USER and AUTH_PASS "
                     "environment variables must be set.\n";
        return std::make_shared<httpserver::http_response>(
            httpserver::http_response::string("Server configuration error")
                .with_status(500));
    }
    if (req.get_user() != expected_user || req.get_pass() != expected_pass) {
        return std::make_shared<httpserver::http_response>(
            httpserver::http_response::unauthorized(
                "Basic", "MyRealm", "Unauthorized"));
    }
    return nullptr;
}

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)
                                 .auth_handler(auth_handler)
                                 .auth_skip_paths({"/health", "/public/*"})};

    ws.on_get("/api", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, authenticated user!");
    });
    ws.on_get("/health", [](const httpserver::http_request&) {
        return httpserver::http_response::string("OK");
    });

    ws.start(true);
    return 0;
}
