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

#include <map>
#include <string>

#include <httpserver.hpp>

// Simple PSK database - in production, use secure storage.
std::map<std::string, std::string> psk_database = {
    {"client1", "0123456789abcdef0123456789abcdef"},
    {"client2", "fedcba9876543210fedcba9876543210"}
};

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
    httpserver::webserver ws{
        httpserver::create_webserver(8080)
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
