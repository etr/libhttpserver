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

// digest_authentication.cpp - per-request HTTP Digest auth check inside
// a lambda handler. The two `unauthorized` returns are intentionally
// indistinguishable to the client to avoid leaking nonce-state info.
//
// NOTE: SHA-256 is used here (RFC 7616). MD5-based Digest auth is broken
// (trivially cracked by offline dictionary attacks against the HA1 hash) and
// must not be used in new deployments. Use SHA-256 or stronger.
//
// The 300-second nonce lifetime balances usability (clients can retry without
// re-authenticating) against replay-attack window. Shorten for higher security.
//
// SECURITY NOTE: Credentials MUST NOT be hardcoded in source code (CWE-798).
// This example loads DIGEST_PASS from the environment.
//
// Usage:
//   export DIGEST_PASS=mysecretpassword
//   ./digest_authentication

#include <cstdlib>
#include <iostream>

#include <httpserver.hpp>

int main() {
    const char* expected_pass = std::getenv("DIGEST_PASS");
    if (!expected_pass) {
        std::cerr << "digest_authentication: DIGEST_PASS environment "
                     "variable must be set.\n";
        return 1;
    }
    const std::string pass = expected_pass;

    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/hello", [pass](const httpserver::http_request& req) {
        using httpserver::http::http_utils;
        if (req.get_digested_user() == "") {
            return httpserver::http_response::unauthorized(
                "Digest", "test@example.com", "FAIL");
        }
        auto result = req.check_digest_auth(
            "test@example.com", pass, 300, 0,
            http_utils::digest_algorithm::SHA256);
        if (result != http_utils::digest_auth_result::OK) {
            return httpserver::http_response::unauthorized(
                "Digest", "test@example.com", "FAIL");
        }
        return httpserver::http_response::string("SUCCESS");
    });

    ws.start(true);
    return 0;
}
