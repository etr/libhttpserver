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

#include <string.h>
#include <sys/types.h>
#include <memory>
#include <string>
#include <utility>

#include <httpserver.hpp>

using namespace httpserver;

static int counter = 0;

ssize_t test_callback (std::shared_ptr<void> closure_data, char* buf, size_t max) {
    if (counter == 2) {
        return -1;
    }
    else {
        memset(buf, 0, max);
        strcat(buf, " test ");
        counter++;
        return std::string(buf).size();
    }
}

class deferred_resource : public http_resource {
    public:
        const std::shared_ptr<http_response> render_GET(const http_request& req) {
            return std::shared_ptr<deferred_response<void> >(new deferred_response<void>(test_callback, nullptr, "cycle callback response"));
        }
};

int main(int argc, char** argv) {
    webserver ws = create_webserver(8080);

    deferred_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}

