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

#ifdef HAVE_WEBSOCKET

#include "httpserver/websocket_handler.hpp"

#include <microhttpd.h>
#include <microhttpd_ws.h>

#if !defined(__MINGW32__)
#include <sys/socket.h>
#endif

#include <cstring>
#include <string>

namespace httpserver {

// websocket_session implementation

websocket_session::websocket_session(MHD_socket sock, struct MHD_UpgradeResponseHandle* urh,
                                     struct MHD_WebSocketStream* ws_stream):
    sock(sock), urh(urh), ws_stream(ws_stream), valid(true) {
}

websocket_session::~websocket_session() {
    if (ws_stream != nullptr) {
        MHD_websocket_stream_free(ws_stream);
    }
    if (urh != nullptr) {
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
    }
}

static bool send_all(MHD_socket sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t ret = send(sock, data + sent, len - sent, 0);
        if (ret <= 0) return false;
        sent += static_cast<size_t>(ret);
    }
    return true;
}

void websocket_session::send_text(const std::string& msg) {
    if (!valid) return;
    char* frame = nullptr;
    size_t frame_len = 0;
    if (MHD_websocket_encode_text(ws_stream, msg.c_str(), msg.size(), 0, &frame, &frame_len, nullptr) == MHD_WEBSOCKET_STATUS_OK) {
        if (!send_all(sock, frame, frame_len)) valid = false;
        MHD_websocket_free(ws_stream, frame);
    }
}

void websocket_session::send_binary(const void* data, size_t len) {
    if (!valid) return;
    char* frame = nullptr;
    size_t frame_len = 0;
    if (MHD_websocket_encode_binary(ws_stream, static_cast<const char*>(data), len, 0, &frame, &frame_len) == MHD_WEBSOCKET_STATUS_OK) {
        if (!send_all(sock, frame, frame_len)) valid = false;
        MHD_websocket_free(ws_stream, frame);
    }
}

void websocket_session::send_ping(const std::string& payload) {
    if (!valid) return;
    char* frame = nullptr;
    size_t frame_len = 0;
    if (MHD_websocket_encode_ping(ws_stream, payload.c_str(), payload.size(), &frame, &frame_len) == MHD_WEBSOCKET_STATUS_OK) {
        if (!send_all(sock, frame, frame_len)) valid = false;
        MHD_websocket_free(ws_stream, frame);
    }
}

void websocket_session::send_pong(const std::string& payload) {
    if (!valid) return;
    char* frame = nullptr;
    size_t frame_len = 0;
    if (MHD_websocket_encode_pong(ws_stream, payload.c_str(), payload.size(), &frame, &frame_len) == MHD_WEBSOCKET_STATUS_OK) {
        if (!send_all(sock, frame, frame_len)) valid = false;
        MHD_websocket_free(ws_stream, frame);
    }
}

void websocket_session::close(uint16_t code, const std::string& reason) {
    if (!valid) return;
    valid = false;
    char* frame = nullptr;
    size_t frame_len = 0;
    if (MHD_websocket_encode_close(ws_stream, code, reason.c_str(), reason.size(), &frame, &frame_len) == MHD_WEBSOCKET_STATUS_OK) {
        send_all(sock, frame, frame_len);
        MHD_websocket_free(ws_stream, frame);
    }
}

bool websocket_session::is_valid() const {
    return valid;
}

// websocket_handler default implementations

void websocket_handler::on_open(websocket_session&) {
}

void websocket_handler::on_binary(websocket_session&, const void*, size_t) {
}

void websocket_handler::on_ping(websocket_session& session, const std::string& payload) {
    session.send_pong(payload);
}

void websocket_handler::on_close(websocket_session&, uint16_t, const std::string&) {
}

}  // namespace httpserver

#endif  // HAVE_WEBSOCKET
