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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_
#define SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_

#ifdef HAVE_WEBSOCKET

#include <microhttpd.h>
#include <microhttpd_ws.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace httpserver {

class http_request;

class websocket_session {
 public:
     void send_text(const std::string& msg);
     void send_binary(const void* data, size_t len);
     void send_ping(const std::string& payload = "");
     void send_pong(const std::string& payload = "");
     void close(uint16_t code = 1000, const std::string& reason = "");
     bool is_valid() const;

 private:
     websocket_session(MHD_socket sock, struct MHD_UpgradeResponseHandle* urh,
                       struct MHD_WebSocketStream* ws_stream);
     ~websocket_session();

     websocket_session(const websocket_session&) = delete;
     websocket_session& operator=(const websocket_session&) = delete;

     MHD_socket sock;
     struct MHD_UpgradeResponseHandle* urh;
     struct MHD_WebSocketStream* ws_stream;
     bool valid;

     friend class webserver;
};

class websocket_handler {
 public:
     virtual ~websocket_handler() = default;

     virtual void on_open(websocket_session& session);
     virtual void on_message(websocket_session& session, std::string_view msg) = 0;
     virtual void on_binary(websocket_session& session, const void* data, size_t len);
     virtual void on_ping(websocket_session& session, std::string_view payload);
     virtual void on_close(websocket_session& session, uint16_t code, const std::string& reason);
};

}  // namespace httpserver

#endif  // HAVE_WEBSOCKET

#endif  // SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_
