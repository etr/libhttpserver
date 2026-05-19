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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
// cpplint errors on chrono and thread because they are replaced (in Chromium) by other google libraries.
// This is not an issue here.
#include <chrono> // NOLINT [build/c++11]
#include <memory>
#include <string>
#include <thread> // NOLINT [build/c++11]

#include <httpserver.hpp>

// Global counter: tracks how many connections have been established.
// Each render_get() call captures a snapshot (reqid) by value.
std::atomic<int> counter;

class deferred_resource : public httpserver::http_resource {
 public:
     httpserver::http_response render_get(const httpserver::http_request&) override {
         int reqid = counter++;
         std::string preamble = "cycle callback response";
         return
             httpserver::http_response::deferred(
                 [reqid, preamble,
                  served = false](std::uint64_t, char* buf,
                                  std::size_t max) mutable -> ssize_t {
                     if (!served) {
                         served = true;
                         std::size_t n = std::min(preamble.size(), max);
                         memcpy(buf, preamble.data(), n);
                         return n;
                     }
                     // only first 5 connections can be established
                     if (reqid >= 5) {
                         return -1;
                     }
                     // respond corresponding request IDs to the clients
                     std::string str = std::to_string(reqid) + " ";
                     memset(buf, 0, max);
                     std::copy(str.begin(), str.end(), buf);
                     // keep sending reqid
                     // sleep(1); ==> adapted for C++11 on non-*Nix systems
                     std::this_thread::sleep_for(std::chrono::seconds(1));
                     return static_cast<ssize_t>(max);
                 });
     }
};

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    auto hwr = std::make_shared<deferred_resource>();
    ws.register_path("/hello", hwr);
    ws.start(true);

    return 0;
}

