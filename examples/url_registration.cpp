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

// url_registration.cpp - all four routing flavors: exact path, prefix
// match, plain regex, and parametric segments with optional constraints.
//
// The exact-path and parametric-arg endpoints register a stateless lambda
// directly. The prefix and regex endpoints share a small class because
// they want the same handler logic on multiple paths and on_* does not
// accept a precompiled-regex form for prefix matching.

#include <memory>
#include <string>

#include <httpserver.hpp>

class echo_path_resource : public httpserver::http_resource {
 public:
    httpserver::http_response render(const httpserver::http_request& req) override {
        return httpserver::http_response::string(
            "Your URL: " + std::string(req.get_path()));
    }
};

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    // Exact path, stateless: the lambda form is the v2.0 recommended idiom.
    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, World!");
    });

    // Prefix-match and regex registration still go through the resource
    // API. The same instance is registered against two routes.
    auto echo = std::make_shared<echo_path_resource>();
    ws.register_prefix("/family", echo);       // /family and /family/*
    ws.register_path("/with_regex_[0-9]+", echo);

    // Parametric segments with per-segment regex constraints (preferred form).
    // The regex restricts arg1 to digits and arg2 to uppercase ASCII, so the
    // router rejects invalid input before the handler is even invoked.
    ws.on_get("/url/with/parametric/args/{arg1|[0-9]+}/and/{arg2|[A-Z]+}",
              [](const httpserver::http_request& req) {
        return httpserver::http_response::string(
            "ARGS: " + std::string(req.get_arg("arg1"))
            + " and " + std::string(req.get_arg("arg2")));
    });

    // Unconstrained parametric segments (accepts any bytes in the URL slot).
    // SECURITY NOTE: with no per-segment constraint the handler receives
    // arbitrary input. Validate or sanitize before using in HTML, SQL, or
    // system calls — raw reflection into an HTML response is reflected XSS
    // (CWE-79); passing directly to a shell or DB is injection (CWE-89/78).
    ws.on_get("/url/with/{arg1}/and/{arg2}",
              [](const httpserver::http_request& req) {
        return httpserver::http_response::string(
            "ARGS: " + std::string(req.get_arg("arg1"))
            + " and " + std::string(req.get_arg("arg2")));
    });

    ws.start(true);
    return 0;
}
