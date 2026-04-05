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
     std::shared_ptr<httpserver::http_response> render_DELETE(const httpserver::http_request&) {
         // Return a 204 No Content response with no body
         return std::make_shared<httpserver::empty_response>(
                 httpserver::http::http_utils::http_no_content);
     }

     std::shared_ptr<httpserver::http_response> render_HEAD(const httpserver::http_request&) {
         // Return a HEAD-only response with headers but no body
         auto response = std::make_shared<httpserver::empty_response>(
                 httpserver::http::http_utils::http_ok,
                 httpserver::empty_response::HEAD_ONLY);
         response->with_header("X-Total-Count", "42");
         return response;
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    no_content_resource ncr;
    ws.register_resource("/items", &ncr);
    ws.start(true);

    return 0;
}
