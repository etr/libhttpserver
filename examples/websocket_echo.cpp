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

#include <iostream>
#include <string>

#include <httpserver.hpp>

class echo_handler : public httpserver::websocket_handler {
 public:
     void on_open(httpserver::websocket_session& session) override {
         std::cout << "WebSocket connection opened" << std::endl;
         session.send_text("Welcome to the echo server!");
     }

     void on_message(httpserver::websocket_session& session, const std::string& msg) override {
         std::cout << "Received: " << msg << std::endl;
         session.send_text("Echo: " + msg);
     }

     void on_close(httpserver::websocket_session& session, uint16_t code, const std::string& reason) override {
         std::cout << "WebSocket closed (code=" << code << ", reason=" << reason << ")" << std::endl;
     }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    echo_handler handler;
    ws.register_ws_resource("/ws", &handler);
    ws.start(true);

    return 0;
}
