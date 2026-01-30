/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

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
#include <string>

#include <httpserver.hpp>

using httpserver::http_request;
using httpserver::http_response;
using httpserver::http_resource;
using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::string_response;
using httpserver::basic_auth_fail_response;

// Simple resource that doesn't need to handle auth itself
class hello_resource : public http_resource {
 public:
    std::shared_ptr<http_response> render_GET(const http_request&) {
        return std::make_shared<string_response>("Hello, authenticated user!", 200, "text/plain");
    }
};

class health_resource : public http_resource {
 public:
    std::shared_ptr<http_response> render_GET(const http_request&) {
        return std::make_shared<string_response>("OK", 200, "text/plain");
    }
};

// Centralized authentication handler
// Returns nullptr to allow the request, or an http_response to reject it
std::shared_ptr<http_response> auth_handler(const http_request& req) {
    if (req.get_user() != "admin" || req.get_pass() != "secret") {
        return std::make_shared<basic_auth_fail_response>("Unauthorized", "MyRealm");
    }
    return nullptr;  // Allow request
}

int main() {
    // Create webserver with centralized authentication
    // - auth_handler: called before every resource's render method
    // - auth_skip_paths: paths that bypass authentication
    webserver ws = create_webserver(8080)
        .auth_handler(auth_handler)
        .auth_skip_paths({"/health", "/public/*"});

    hello_resource hello;
    health_resource health;

    ws.register_resource("/api", &hello);
    ws.register_resource("/health", &health);

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
