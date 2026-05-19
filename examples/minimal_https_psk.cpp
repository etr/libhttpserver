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

// minimal_https_psk.cpp - TLS with pre-shared keys instead of certificates.
// The PSK lookup callback is invoked once per handshake to resolve a
// client-supplied identity into the shared secret.

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include <httpserver.hpp>

// PSK database loaded from environment variables at startup.
//
// SECURITY NOTE: Never hardcode PSK keys in source code (CWE-321 / CWE-798).
// The values below are illustrative placeholders. In production:
//   - Load keys from environment variables, a key file, or a secrets manager.
//   - Use at least 128 bits of cryptographically-random key material.
//   - Rotate keys regularly.
//
// Example environment setup before running:
//   export PSK_CLIENT1=0123456789abcdef0123456789abcdef
//   export PSK_CLIENT2=fedcba9876543210fedcba9876543210
//   ./minimal_https_psk
static std::map<std::string, std::string> build_psk_database() {
    std::map<std::string, std::string> db;
    const char* k1 = std::getenv("PSK_CLIENT1");
    const char* k2 = std::getenv("PSK_CLIENT2");
    if (!k1 || !k2) {
        std::cerr << "minimal_https_psk: PSK_CLIENT1 and PSK_CLIENT2 "
                     "environment variables must be set.\n";
        std::exit(1);
    }
    db["client1"] = k1;
    db["client2"] = k2;
    return db;
}

static std::map<std::string, std::string> psk_database = build_psk_database();

// PSK credential handler callback. Returns the hex-encoded PSK for the
// given username, or an empty string for unknown identities.
std::string psk_handler(const std::string& username) {
    auto it = psk_database.find(username);
    if (it != psk_database.end()) {
        return it->second;
    }
    return "";
}

int main() {
    // The priority string locks to TLS 1.2 because GnuTLS external PSK in
    // TLS 1.3 requires a different mechanism (session-ticket-based resumption).
    // Port 8443 signals HTTPS semantics by convention.
    httpserver::webserver ws{
        httpserver::create_webserver(8443)
            .use_ssl()
            .cred_type(httpserver::http::http_utils::PSK)
            .psk_cred_handler(psk_handler)
            .https_priorities("NORMAL:-VERS-TLS-ALL:+VERS-TLS1.2:+PSK:+DHE-PSK")};

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, World (via TLS-PSK)!");
    });

    ws.start(true);
    return 0;
}
