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

class hello_world_resource : public http_resource {
public:
    const std::shared_ptr<http_response> render(http_request&) {
        return std::shared_ptr<http_response>(new string_response("Hello, World!"));
    }
};

class handling_multiple_resource : public http_resource {
public:
    const std::shared_ptr<http_response> render(http_request& req) {
        return std::shared_ptr<http_response>(new string_response("Your URL: " + req.get_path()));
    }
};

class url_args_resource : public http_resource {
public:
    const std::shared_ptr<http_response> render(http_request& req) {
        return std::shared_ptr<http_response>(new string_response("ARGS: " + req.get_arg("arg1") + " and " + req.get_arg("arg2")));
    }
};

int main(int argc, char** argv) {
    webserver ws = create_webserver(8080);

    hello_world_resource hwr;
    ws.register_resource("/hello", &hwr);

    handling_multiple_resource hmr;
    ws.register_resource("/family", &hmr, true);
    ws.register_resource("/with_regex_[0-9]+", &hmr);

    url_args_resource uar;
    ws.register_resource("/url/with/{arg1}/and/{arg2}", &uar);
    ws.register_resource("/url/with/parametric/args/{arg1|[0-9]+}/and/{arg2|[A-Z]+}", &uar);

    ws.start(true);

    return 0;
}
