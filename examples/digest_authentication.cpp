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

#include <memory>

#include <httpserver.hpp>

#define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

class digest_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request& req) {
         using httpserver::http::http_utils;
         if (req.get_digested_user() == "") {
             return std::make_shared<httpserver::http_response>(httpserver::http_response::unauthorized("Digest", "test@example.com", "FAIL"));
         } else {
             auto result = req.check_digest_auth("test@example.com", "mypass", 300, 0, http_utils::digest_algorithm::MD5);
             if (result == http_utils::digest_auth_result::NONCE_STALE) {
                 return std::make_shared<httpserver::http_response>(httpserver::http_response::unauthorized("Digest", "test@example.com", "FAIL"));
             } else if (result != http_utils::digest_auth_result::OK) {
                 return std::make_shared<httpserver::http_response>(httpserver::http_response::unauthorized("Digest", "test@example.com", "FAIL"));
             }
         }
         return std::make_shared<httpserver::http_response>(httpserver::http_response::string("SUCCESS"));
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    digest_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}
