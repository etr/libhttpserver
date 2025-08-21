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

#include <httpserver.hpp>

std::shared_ptr<httpserver::http_response> not_found_custom(const httpserver::http_request&) {
    return std::shared_ptr<httpserver::string_response>(new httpserver::string_response("Not found custom", 404, "text/plain"));
}

std::shared_ptr<httpserver::http_response> not_allowed_custom(const httpserver::http_request&) {
    return std::shared_ptr<httpserver::string_response>(new httpserver::string_response("Not allowed custom", 405, "text/plain"));
}

class hello_world_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render(const httpserver::http_request&) {
         return std::shared_ptr<httpserver::http_response>(new httpserver::string_response("Hello, World!"));
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .not_found_resource(not_found_custom)
        .method_not_allowed_resource(not_allowed_custom);

    hello_world_resource hwr;
    hwr.disallow_all();
    hwr.set_allowing("GET", true);
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}
