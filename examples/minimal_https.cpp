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

// minimal_https.cpp - enable HTTPS via the use_ssl / https_mem_key /
// https_mem_cert chain on create_webserver.
//
// The https_priorities call sets an explicit GnuTLS cipher-priority string
// that allows TLS 1.2 and TLS 1.3 with safe renegotiation. Without it,
// GnuTLS may fall back to TLS 1.0/1.1 depending on the system configuration.
// Port 8443 is used (convention for HTTPS on non-privileged ports).

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8443)
                                 .use_ssl()
                                 .https_mem_key("key.pem")
                                 .https_mem_cert("cert.pem")
                                 .https_priorities(
                                     "NORMAL:-VERS-TLS-ALL"
                                     ":+VERS-TLS1.2:+VERS-TLS1.3"
                                     ":%SAFE_RENEGOTIATION")};

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, World!");
    });

    ws.start(true);
    return 0;
}
