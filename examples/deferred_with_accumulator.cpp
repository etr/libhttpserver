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
#include <cstring>
// cpplint errors on chrono and thread because they are replaced (in Chromium) by other google libraries.
// This is not an issue here.
#include <chrono> // NOLINT [build/c++11]
#include <thread> // NOLINT [build/c++11]

#include <httpserver.hpp>

std::atomic<int> counter;

ssize_t test_callback(std::shared_ptr<std::atomic<int> > closure_data, char* buf, size_t max) {
    int reqid;
    if (closure_data == nullptr) {
        reqid = -1;
    } else {
        reqid = *closure_data;
    }

    // only first 5 connections can be established
    if (reqid >= 5) {
        return -1;
    } else {
        // respond corresponding request IDs to the clients
        std::string str = "";
        str += std::to_string(reqid) + " ";
        memset(buf, 0, max);
        std::copy(str.begin(), str.end(), buf);

        // keep sending reqid
        // sleep(1); ==> adapted for C++11 on non-*Nix systems
        std::this_thread::sleep_for(std::chrono::seconds(1));

        return (ssize_t)max;
    }
}

class deferred_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         std::shared_ptr<std::atomic<int> > closure_data(new std::atomic<int>(counter++));
         return std::shared_ptr<httpserver::deferred_response<std::atomic<int> > >(new httpserver::deferred_response<std::atomic<int> >(test_callback, closure_data, "cycle callback response"));
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    deferred_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}

