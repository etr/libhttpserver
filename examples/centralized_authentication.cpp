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
// that runs before every request. The handler returns std::nullopt to
// accept, or an http_response to reject. auth_skip_paths lists routes
// that bypass auth entirely.
//
// SECURITY NOTE: Credentials MUST NOT be hardcoded in source code (CWE-798).
// This example loads AUTH_USER and AUTH_PASS from environment variables.
//
// CONSTANT-TIME NOTE: The credential comparison below uses
// `constant_time_equal` (defined further down), which is the
// production-ready form: it runs in time independent of the secret
// bytes, closing the CWE-208 timing side-channel that a naive
// std::string::operator!= (short-circuiting on the first differing
// byte) would open.
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

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <httpserver.hpp>

namespace {

// Constant-time equality: the runtime depends only on a.size(), never on
// where a mismatch occurs, closing the CWE-208 timing side-channel. The
// length is not the secret here, so an early length check is fine (this
// mirrors OpenSSL's CRYPTO_memcmp, which also requires equal lengths).
bool constant_time_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

}  // namespace

// Returns std::nullopt to allow the request, or an engaged optional
// carrying an http_response to reject it (TASK-054). For illustration;
// production must read AUTH_USER and AUTH_PASS once at startup into
// immutable storage -- calling std::getenv on every request is not
// thread-safe with respect to concurrent setenv/putenv from other
// threads in the same process.
std::optional<httpserver::http_response> auth_handler(
        const httpserver::http_request& req) {
    const char* expected_user = std::getenv("AUTH_USER");
    const char* expected_pass = std::getenv("AUTH_PASS");
    if (!expected_user || !expected_pass) {
        std::cerr << "centralized_authentication: AUTH_USER and AUTH_PASS "
                     "environment variables must be set.\n";
        return httpserver::http_response::string("Server configuration error")
            .with_status(500);
    }
    // The || short-circuits at the *field* level (user then pass), which
    // does not leak per-byte password timing -- each field compare is
    // itself constant-time.
    if (!constant_time_equal(req.get_user(), expected_user) ||
            !constant_time_equal(req.get_pass(), expected_pass)) {
        return httpserver::http_response::unauthorized(
            "Basic", "MyRealm", "Unauthorized");
    }
    return std::nullopt;
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
