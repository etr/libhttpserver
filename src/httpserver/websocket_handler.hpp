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

// TASK-020 / TASK-034: <microhttpd.h> and <microhttpd_ws.h> are
// deliberately NOT included from this public header. The class below
// uses two MHD-defined struct types only by pointer (forward-declared
// at file scope below) and `MHD_socket` (an integer typedef -- `int`
// on POSIX, `SOCKET` (`UINT_PTR`) on Windows). Because typedef-names
// cannot be forward-declared, the public surface uses `std::intptr_t`
// -- which is at least as wide as `MHD_socket` on every platform we
// support. A static_assert in src/websocket_handler.cpp pins that
// invariant where <microhttpd.h> is reachable.
//
// TASK-034: the public declarations below are now visible
// unconditionally (PRD-FLG-REQ-001). Method bodies live in
// src/websocket_handler.cpp; on HAVE_WEBSOCKET-off builds the
// definitions in that TU either throw feature_unavailable or are no-ops
// (see the file for the per-method policy).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct MHD_UpgradeResponseHandle;
struct MHD_WebSocketStream;

namespace httpserver {

class http_request;
namespace detail { class webserver_impl; }

class websocket_session {
 public:
     void send_text(const std::string& msg);
     void send_binary(const void* data, size_t len);
     void send_ping(const std::string& payload = "");
     void send_pong(const std::string& payload = "");
     void close(uint16_t code = 1000, const std::string& reason = "");
     bool is_valid() const;

 private:
     // `sock` carries an MHD_socket value; the public-header type is
     // std::intptr_t so this header does not need <microhttpd.h>.
     // src/websocket_handler.cpp casts back to MHD_socket at the
     // boundary and static_asserts the underlying-width invariant.
     websocket_session(std::intptr_t sock, struct MHD_UpgradeResponseHandle* urh,
                       struct MHD_WebSocketStream* ws_stream);
     ~websocket_session();

     websocket_session(const websocket_session&) = delete;
     websocket_session& operator=(const websocket_session&) = delete;

     std::intptr_t sock;
     struct MHD_UpgradeResponseHandle* urh;
     struct MHD_WebSocketStream* ws_stream;
     bool valid;

     friend class webserver;
     friend class detail::webserver_impl;  // TASK-014: PIMPL upgrade path
};

class websocket_handler {
 public:
     virtual ~websocket_handler() = default;

     virtual void on_open(websocket_session& session);
     virtual void on_message(websocket_session& session, std::string_view msg) = 0;
     // TASK-034 incidental cleanup: the previous header had on_binary and
     // on_ping declared twice (identical signatures, never spotted because
     // identical-redeclaration is legal). Removing the duplicates lets the
     // class compile on HAVE_WEBSOCKET-off builds where the header is now
     // always included via the umbrella.
     virtual void on_binary(websocket_session& session, const void* data, size_t len);
     virtual void on_ping(websocket_session& session, std::string_view payload);
     virtual void on_close(websocket_session& session, uint16_t code, const std::string& reason);
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_
