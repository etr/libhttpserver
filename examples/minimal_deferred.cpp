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

#include <cstring>
#include <httpserver.hpp>

static int counter = 0;

ssize_t test_callback(std::shared_ptr<void> closure_data, char* buf, size_t max) {
    std::ignore = closure_data;

    if (counter == 2) {
        return -1;
    } else {
        memset(buf, 0, max);
        snprintf(buf, max, "%s", " test ");
        counter++;
        return std::string(buf).size();
    }
}

class deferred_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         return std::shared_ptr<httpserver::deferred_response<void> >(new httpserver::deferred_response<void>(test_callback, nullptr, "cycle callback response"));
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    deferred_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}

