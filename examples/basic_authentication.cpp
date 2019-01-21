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

using namespace httpserver;

class user_pass_resource : public httpserver::http_resource
{
    public:
        const std::shared_ptr<http_response> render_GET(http_request& req)
        {
            if (req.get_user() != "myuser" || req.get_pass() != "mypass")
            {
                return std::shared_ptr<basic_auth_fail_response>(new basic_auth_fail_response("FAIL", "test@example.com"));
            }
            return std::shared_ptr<string_response>(new string_response(req.get_user() + " " + req.get_pass(), 200, "text/plain"));
        }
};

int main(int argc, char** argv) {
    webserver ws = create_webserver(8080);

    user_pass_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}
