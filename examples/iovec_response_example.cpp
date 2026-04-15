/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <httpserver.hpp>

class iovec_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         // Build a response from multiple separate buffers without copying
         std::vector<std::string> parts;
         parts.push_back("{\"header\": \"value\", ");
         parts.push_back("\"items\": [1, 2, 3], ");
         parts.push_back("\"footer\": \"end\"}");

         return std::make_shared<httpserver::iovec_response>(
                 std::move(parts), 200, "application/json");
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    iovec_resource ir;
    ws.register_resource("/data", &ir);
    ws.start(true);

    return 0;
}
