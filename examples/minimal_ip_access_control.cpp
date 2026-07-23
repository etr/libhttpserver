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

// minimal_ip_access_control.cpp - demonstrate the IP access-control API.
//
// Two lists, selected by create_webserver::default_policy():
//   - deny_ip / remove_denied_ip : the exception list under the default
//     ACCEPT policy (permit everyone except these).
//   - allow_ip / remove_allowed_ip : the exception list under the REJECT
//     policy (permit only these). Under ACCEPT, an allow entry also
//     overrides a matching deny entry (allow wins).

#include <httpserver.hpp>

int main() {
    // Default ACCEPT policy: block-list mode. Everyone is admitted except
    // addresses on the deny list.
    httpserver::webserver ws{httpserver::create_webserver(8080)};
    ws.deny_ip("10.0.0.1");

    // Allow-list mode: flip the default to REJECT, then permit only the
    // addresses you allow_ip. Everyone else is refused at the policy
    // callback.
    //
    //   httpserver::webserver ws{
    //       httpserver::create_webserver(8080)
    //           .default_policy(httpserver::http::http_utils::REJECT)};
    //   ws.allow_ip("127.0.0.1");   // only localhost may connect

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, World!");
    });

    ws.start(true);
    return 0;
}
