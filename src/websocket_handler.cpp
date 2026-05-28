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

#include "httpserver/websocket_handler.hpp"
#include "httpserver/feature_unavailable.hpp"

#ifdef HAVE_WEBSOCKET

#include <microhttpd.h>
#include <microhttpd_ws.h>

#if !defined(__MINGW32__)
#include <sys/socket.h>
#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace httpserver {

// TASK-020-review: pin the public-header `std::uintptr_t` choice for the
// websocket socket handle to libmicrohttpd's `MHD_socket` typedef. Using
// uintptr_t (unsigned) rather than intptr_t preserves all bit patterns of
// SOCKET/MHD_socket through the round-trip on Windows where SOCKET is
// UINT_PTR and valid descriptors can have the high bit set (CWE-681).
static_assert(sizeof(MHD_socket) <= sizeof(std::uintptr_t),
              "MHD_socket is wider than std::uintptr_t on this platform; "
              "websocket_session::sock's public type must be widened.");

// websocket_session implementation

websocket_session::websocket_session(std::uintptr_t sock, struct MHD_UpgradeResponseHandle* urh,
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
        if (!send_all(static_cast<MHD_socket>(sock), frame, frame_len)) valid = false;
        MHD_websocket_free(ws_stream, frame);
    }
}

void websocket_session::send_binary(const void* data, size_t len) {
    if (!valid) return;
    char* frame = nullptr;
    size_t frame_len = 0;
    if (MHD_websocket_encode_binary(ws_stream, static_cast<const char*>(data), len, 0, &frame, &frame_len) == MHD_WEBSOCKET_STATUS_OK) {
        if (!send_all(static_cast<MHD_socket>(sock), frame, frame_len)) valid = false;
        MHD_websocket_free(ws_stream, frame);
    }
}

// Shared scaffolding for the ping/pong control frames: both MHD encode
// functions share an identical signature and the surrounding
// encode -> send_all -> free dance is byte-identical between them.
// Type the encoder via decltype so we pick up MHD's exact return type
// (`enum MHD_WEBSOCKET_STATUS`, not `int`) without restating it.
using control_frame_encoder = decltype(&MHD_websocket_encode_ping);
static_assert(std::is_same_v<control_frame_encoder, decltype(&MHD_websocket_encode_pong)>,
              "MHD_websocket_encode_ping and _pong must share a signature "
              "for send_control_frame's function-pointer parameter to be valid.");

static void send_control_frame(control_frame_encoder encode,
                               struct MHD_WebSocketStream* ws_stream,
                               MHD_socket sock,
                               const std::string& payload,
                               bool& valid) {
    char* frame = nullptr;
    size_t frame_len = 0;
    if (encode(ws_stream, payload.c_str(), payload.size(), &frame, &frame_len) == MHD_WEBSOCKET_STATUS_OK) {
        if (!send_all(sock, frame, frame_len)) valid = false;
        MHD_websocket_free(ws_stream, frame);
    } else {
        // Encode failure (e.g. stream already in error state): treat as a
        // closed session so callers stop issuing further frames on this
        // connection. Previously encode failures were silently swallowed,
        // leaving valid==true despite a broken stream. (finding #2)
        valid = false;
    }
}

void websocket_session::send_ping(const std::string& payload) {
    if (!valid) return;
    send_control_frame(&MHD_websocket_encode_ping, ws_stream,
                       static_cast<MHD_socket>(sock), payload, valid);
}

void websocket_session::send_pong(const std::string& payload) {
    if (!valid) return;
    send_control_frame(&MHD_websocket_encode_pong, ws_stream,
                       static_cast<MHD_socket>(sock), payload, valid);
}

void websocket_session::close(uint16_t code, const std::string& reason) {
    if (!valid) return;
    valid = false;
    char* frame = nullptr;
    size_t frame_len = 0;
    if (MHD_websocket_encode_close(ws_stream, code, reason.c_str(), reason.size(), &frame, &frame_len) == MHD_WEBSOCKET_STATUS_OK) {
        send_all(static_cast<MHD_socket>(sock), frame, frame_len);
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

void websocket_handler::on_ping(websocket_session& session, std::string_view payload) {
    session.send_pong(std::string(payload));
}

void websocket_handler::on_close(websocket_session&, uint16_t, const std::string&) {
}

}  // namespace httpserver

#else   // !HAVE_WEBSOCKET

// TASK-034: WebSocket compiled out. The class declarations are public
// and unconditional (PRD-FLG-REQ-001), so we still need out-of-line
// definitions for every member function or the link step will fail.
// The policy (architecture spec §7):
//   - constructors/destructor are no-ops (the session never holds a live
//     handle anyway);
//   - is_valid() returns false;
//   - every send_*/close call throws feature_unavailable;
//   - websocket_handler's defaulted virtual hooks are no-ops.
//
// Calling code is expected to first consult webserver::features().websocket
// and avoid this path entirely on disabled builds.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace httpserver {

websocket_session::websocket_session(std::uintptr_t sock,
                                     struct MHD_UpgradeResponseHandle* urh,
                                     struct MHD_WebSocketStream* ws_stream)
    : sock(sock), urh(urh), ws_stream(ws_stream), valid(false) {}

websocket_session::~websocket_session() = default;

void websocket_session::send_text(const std::string&) {
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
}

void websocket_session::send_binary(const void*, size_t) {
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
}

void websocket_session::send_ping(const std::string&) {
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
}

void websocket_session::send_pong(const std::string&) {
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
}

void websocket_session::close(uint16_t, const std::string&) {
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
}

bool websocket_session::is_valid() const {
    return false;
}

void websocket_handler::on_open(websocket_session&) {}
void websocket_handler::on_binary(websocket_session&, const void*, size_t) {}
void websocket_handler::on_ping(websocket_session&, std::string_view) {}
void websocket_handler::on_close(websocket_session&, uint16_t, const std::string&) {}

}  // namespace httpserver

#endif  // HAVE_WEBSOCKET
