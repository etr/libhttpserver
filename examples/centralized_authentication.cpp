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

#include <memory>

#include <httpserver.hpp>

// Returns nullptr to allow the request, or an http_response to reject it.
std::shared_ptr<httpserver::http_response> auth_handler(
        const httpserver::http_request& req) {
    if (req.get_user() != "admin" || req.get_pass() != "secret") {
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

// Usage:
//   # Start the server
//   ./centralized_authentication
//
//   # Without auth - should get 401 Unauthorized
//   curl -v http://localhost:8080/api
//
//   # With valid auth - should get 200 OK
//   curl -u admin:secret http://localhost:8080/api
//
//   # Health endpoint (skip path) - works without auth
//   curl http://localhost:8080/health
