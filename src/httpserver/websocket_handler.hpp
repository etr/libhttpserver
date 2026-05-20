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

/**
 * Server-side handle for an upgraded WebSocket connection.
 *
 * Passed by reference to every @ref websocket_handler callback. The
 * lifetime is bounded by the upgrade callback; do not retain a reference
 * beyond the handler's call frame.
 *
 * On a `HAVE_WEBSOCKET`-off build every send-side method throws
 * @ref feature_unavailable (`"websocket"`, `"HAVE_WEBSOCKET"`) so user
 * code linked against an auth-only build gets a uniform exception type
 * rather than a link-time error. `is_valid()` returns `false` in that
 * configuration.
 */
class websocket_session {
 public:
     /**
      * Send a UTF-8 text frame to the peer.
      *
      * @param msg the text payload; must be valid UTF-8.
      * @throws feature_unavailable on a `HAVE_WEBSOCKET`-off build.
      */
     void send_text(const std::string& msg);
     /**
      * Send a binary frame to the peer.
      *
      * @param data pointer to the binary payload (not copied; must
      *             remain valid until this call returns).
      * @param len  payload size in bytes.
      * @throws feature_unavailable on a `HAVE_WEBSOCKET`-off build.
      */
     void send_binary(const void* data, size_t len);
     /**
      * Send a Ping control frame.
      *
      * @param payload optional payload echoed back in the matching Pong.
      * @throws feature_unavailable on a `HAVE_WEBSOCKET`-off build.
      */
     void send_ping(const std::string& payload = "");
     /**
      * Send a Pong control frame (typically in response to a Ping).
      *
      * @param payload optional payload (the Ping's payload, per RFC 6455).
      * @throws feature_unavailable on a `HAVE_WEBSOCKET`-off build.
      */
     void send_pong(const std::string& payload = "");
     /**
      * Send a Close frame and tear down the underlying TCP connection.
      *
      * @param code   the WebSocket close code (default `1000` = normal closure).
      * @param reason optional UTF-8 close reason.
      * @throws feature_unavailable on a `HAVE_WEBSOCKET`-off build.
      */
     void close(uint16_t code = 1000, const std::string& reason = "");
     /**
      * @return `true` if the session is still open (close has not been
      *         called and the socket has not been torn down by the peer).
      *         Always `false` on `HAVE_WEBSOCKET`-off builds.
      */
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

/**
 * Subclass to handle the lifecycle of an upgraded WebSocket connection.
 *
 * Register an instance with @ref webserver::register_ws_resource. Every
 * callback is invoked on the MHD worker thread servicing the upgrade;
 * implementations MUST be thread-safe with respect to any state they
 * share across sessions or with other handlers.
 */
class websocket_handler {
 public:
     virtual ~websocket_handler() = default;

     /**
      * Invoked immediately after the WebSocket handshake completes.
      *
      * The default implementation is a no-op.
      *
      * @param session the freshly upgraded session; valid until the
      *                matching @ref on_close.
      */
     virtual void on_open(websocket_session& session);
     /**
      * Invoked when a text frame is received from the peer.
      *
      * Pure virtual: every concrete handler must implement this.
      *
      * @param session the active session.
      * @param msg     the UTF-8 text payload; the view is valid only for
      *                the duration of the call.
      */
     virtual void on_message(websocket_session& session, std::string_view msg) = 0;
     // TASK-034 incidental cleanup: the previous header had on_binary and
     // on_ping declared twice (identical signatures, never spotted because
     // identical-redeclaration is legal). Removing the duplicates lets the
     // class compile on HAVE_WEBSOCKET-off builds where the header is now
     // always included via the umbrella.
     /**
      * Invoked when a binary frame is received from the peer.
      *
      * The default implementation is a no-op; override to consume binary frames.
      *
      * @param session the active session.
      * @param data    pointer to the binary payload (valid for the call only).
      * @param len     payload size in bytes.
      */
     virtual void on_binary(websocket_session& session, const void* data, size_t len);
     /**
      * Invoked when a Ping control frame is received from the peer.
      *
      * The default implementation echoes the payload back via
      * @ref websocket_session::send_pong. Override to suppress that
      * behaviour.
      *
      * @param session the active session.
      * @param payload the Ping payload (valid for the call only).
      */
     virtual void on_ping(websocket_session& session, std::string_view payload);
     /**
      * Invoked once when the session is closed (peer-initiated or local).
      *
      * The default implementation is a no-op. After this call returns
      * the @p session reference is no longer valid.
      *
      * @param session the closing session.
      * @param code    the WebSocket close code as received or sent.
      * @param reason  the UTF-8 close reason; empty if none was provided.
      */
     virtual void on_close(websocket_session& session, uint16_t code, const std::string& reason);
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_WEBSOCKET_HANDLER_HPP_
