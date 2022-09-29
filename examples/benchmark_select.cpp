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

#include <cstdlib>
#include <memory>

#include <httpserver.hpp>

#define PATH "/plaintext"
#define BODY "Hello, World!"

class hello_world_resource : public httpserver::http_resource {
 public:
     explicit hello_world_resource(const std::shared_ptr<httpserver::http_response>& resp):
         resp(resp) {
     }

     std::shared_ptr<httpserver::http_response> render(const httpserver::http_request&) {
         return resp;
     }

 private:
     std::shared_ptr<httpserver::http_response> resp;
};

int main(int argc, char** argv) {
    std::ignore = argc;

    httpserver::webserver ws = httpserver::create_webserver(atoi(argv[1]))
        .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
        .max_threads(atoi(argv[2]));

    std::shared_ptr<httpserver::http_response> hello = std::shared_ptr<httpserver::http_response>(new httpserver::string_response(BODY, 200));
    hello->with_header("Server", "libhttpserver");

    hello_world_resource hwr(hello);
    ws.register_resource(PATH, &hwr, false);

    ws.start(true);

    return 0;
}
