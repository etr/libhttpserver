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
#include <string>
#include <utility>

#include <httpserver.hpp>

#define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

using namespace httpserver;

class digest_resource : public httpserver::http_resource {
public:
    const std::shared_ptr<http_response> render_GET(const http_request& req) {
        if (req.get_digested_user() == "") {
            return std::shared_ptr<digest_auth_fail_response>(new digest_auth_fail_response("FAIL", "test@example.com", MY_OPAQUE, true));
        }
        else {
            bool reload_nonce = false;
            if(!req.check_digest_auth("test@example.com", "mypass", 300, reload_nonce)) {
                return std::shared_ptr<digest_auth_fail_response>(new digest_auth_fail_response("FAIL", "test@example.com", MY_OPAQUE, reload_nonce));
            }
        }
        return std::shared_ptr<string_response>(new string_response("SUCCESS", 200, "text/plain"));
    }
};

int main(int argc, char** argv) {
    webserver ws = create_webserver(8080);

    digest_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
}
