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

// minimal_ip_ban.cpp - demonstrate the v2.0 IP block API.
//
// TASK-029: the v2.0 public IP-control surface is the pair block_ip /
// unblock_ip, usable under the default ACCEPT policy. The historical
// allow_ip / disallow_ip pair (under REJECT) was dropped.

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.block_ip("10.0.0.1");

    ws.on_get("/hello", [](const httpserver::http_request&) {
        return httpserver::http_response::string("Hello, World!");
    });

    ws.start(true);
    return 0;
}
