/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

     This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by the
   Free Software Foundation; either version 2.1 of the License, or (at your
   option) any later version.

     This library is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
   details.

     You should have received a copy of the GNU Lesser General Public License
   along with this library; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

#include <httpserver.hpp>

class hello_world_resource : public httpserver::http_resource {
     public:
    std::shared_ptr<httpserver::http_response> render(const httpserver::http_request&) {
        return std::shared_ptr<httpserver::http_response>(
            new string_response("Hello, World!"));
    }
};

std::string psk_callback(const std::string& username) {
    // Known identity from the database.
    auto identity = std::string {"oFWv9Cv3N9tUsm"};

    // Pre-shared key in hex-encoded format.
    auto key = std::string {
        "467427f5a4696f8e0ac285f36840efcbf6fd8da88703d26ff68c1faac7f4e2ae"};

    return (username == identity) ? key : std::string {};
}

int main() {
#ifdef HAVE_GNUTLS
    httpserver::webserver ws =
        httpserver::create_webserver(8080)
            .use_ssl()
            .https_priorities("NORMAL:+PSK:+ECDHE-PSK:+DHE-PSK")
            .cred_type(httpserver::http::http_utils::PSK)
            .psk_cred_handler(psk_callback);

    hello_world_resource hwr;
    ws.register_resource("/hello", &hwr);
    ws.start(true);

    return 0;
#else
    return -1;
#endif
}
