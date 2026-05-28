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

#include <httpserver.hpp>

class no_content_resource : public httpserver::http_resource {
 public:
     httpserver::http_response render_delete(const httpserver::http_request&) override {
         return httpserver::http_response::empty();
     }

     httpserver::http_response render_head(const httpserver::http_request&) override {
         // libhttpserver strips the body automatically for HEAD requests.
         return httpserver::http_response::empty()
                    .with_status(httpserver::http::http_utils::http_ok)
                    .with_header("X-Total-Count", "42");
     }
};

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    auto ncr = std::make_shared<no_content_resource>();
    ws.register_path("/items", ncr);
    ws.start(true);

    return 0;
}
