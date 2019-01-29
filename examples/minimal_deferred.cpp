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

#include <atomic>
#include <httpserver.hpp>

using namespace httpserver;

std::atomic<int> reqid;

typedef struct {
    int reqid;
} connection;

ssize_t test_callback (void* data, char* buf, size_t max) {
    int reqid;
    if (data == nullptr) {
        reqid = -1;
    } else {
        reqid = static_cast<connection*>(data)->reqid;
    }

    // only first 5 connections can be established
    if (reqid >= 5) {
        return -1;
    }

    // respond corresponding request IDs to the clients
    std::string str = "";
    str += std::to_string(reqid) + " ";
    memset(buf, 0, max);
    std::copy(str.begin(), str.end(), buf);

    // keep sending reqid
    sleep(1);
    return (ssize_t)max;
}

void test_cleanup (void** data)
{
    if (*data != nullptr) {
            delete static_cast<connection*>(*data);
    }
    data = nullptr;
}

class deferred_resource : public http_resource {
    public:
        const std::shared_ptr<http_response> render_GET(const http_request& req) {
            // private data of new connections
            auto priv_data = new connection();
            priv_data->reqid = reqid++;

            auto response = std::make_shared<deferred_response>(test_callback, priv_data, test_cleanup,
                                                                "cycle callback response");
            return response;
        }
};

int main(int argc, char** argv) {
    reqid.store(0);

    webserver ws = create_webserver(8080);

    deferred_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}
