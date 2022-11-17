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

class user_pass_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request& req) {
         if (req.get_user() != "myuser" || req.get_pass() != "mypass") {
             return std::shared_ptr<httpserver::basic_auth_fail_response>(new httpserver::basic_auth_fail_response("FAIL", "test@example.com"));
         }

         return std::shared_ptr<httpserver::string_response>(new httpserver::string_response(std::string(req.get_user()) + " " + std::string(req.get_pass()), 200, "text/plain"));
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    user_pass_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}
