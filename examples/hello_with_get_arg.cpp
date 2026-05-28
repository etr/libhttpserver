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

// hello_with_get_arg.cpp - read a query-string argument inside a lambda
// handler. Try: curl 'http://localhost:8080/hello?name=world'
//
// NOTE: The response is plain text. If you adapt this for an HTML response,
// you must HTML-entity-encode user-supplied input before inserting it into
// the document body — raw reflection into HTML introduces reflected XSS
// (CWE-79). The explicit "text/plain" content type below signals that
// context and prevents browsers from sniffing the body as HTML.

#include <string>

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/hello", [](const httpserver::http_request& req) {
        // Explicit "text/plain" avoids browser content-type sniffing.
        // Never reflect user input into an HTML response without encoding.
        std::string body;
        body.reserve(7 + req.get_arg("name").get_flat_value().size());
        body = "Hello: ";
        body += req.get_arg("name");
        return httpserver::http_response::string(body, "text/plain");
    });

    ws.start(true);
    return 0;
}
