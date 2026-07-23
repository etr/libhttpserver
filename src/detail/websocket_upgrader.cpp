/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// websocket_upgrader behavior service (DR-014 §4.11). Logic moved verbatim
// out of detail/webserver_websocket.cpp (the frame helpers, the handshake
// validation, the upgrade completion, and the upgrade callback). The
// try_handle_websocket_upgrade webserver_impl forwarder stays there. The
// whole functional body is HAVE_WEBSOCKET-gated; the ws_upgrade_data impl
// back-pointer was unused and is dropped.

#include "httpserver/detail/websocket_upgrader.hpp"

#ifdef HAVE_WEBSOCKET

#include <microhttpd.h>
#include <microhttpd_ws.h>
#include <strings.h>
#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/connection_context.hpp"
#include "httpserver/detail/ws_registry.hpp"

namespace httpserver {
namespace detail {

namespace {

// RFC 6455 §5.5.1: a CLOSE frame's payload starts with a 2-byte status code
// (default 1000 "normal closure") followed by an optional UTF-8 reason.
void handle_close_frame(websocket_handler* handler, websocket_session& session,
                        const char* frame_data, size_t frame_len) {
    uint16_t close_code = 1000;
    std::string close_reason;
    if (frame_len >= 2) {
        close_code = static_cast<uint16_t>(
            (static_cast<unsigned char>(frame_data[0]) << 8) |
             static_cast<unsigned char>(frame_data[1]));
        if (frame_len > 2) {
            close_reason.assign(frame_data + 2, frame_len - 2);
        }
    }
    handler->on_close(session, close_code, close_reason);
    // Echo the close back and end the loop.
    session.close(close_code, close_reason);
}

void dispatch_websocket_frame(int status, struct MHD_WebSocketStream* ws_stream,
                              websocket_handler* handler,
                              websocket_session& session,
                              char* frame_data, size_t frame_len) {
    switch (status) {
        case MHD_WEBSOCKET_STATUS_TEXT_FRAME:
            handler->on_message(session, std::string_view(frame_data, frame_len));
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_BINARY_FRAME:
            handler->on_binary(session, frame_data, frame_len);
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_PING_FRAME:
            handler->on_ping(session, std::string_view(frame_data, frame_len));
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_CLOSE_FRAME:
            handle_close_frame(handler, session, frame_data, frame_len);
            MHD_websocket_free(ws_stream, frame_data);
            break;
        case MHD_WEBSOCKET_STATUS_OK:
            // Need more data - go back to recv.
            if (frame_data != nullptr) MHD_websocket_free(ws_stream, frame_data);
            break;
        default:
            // Protocol error or unknown frame.
            if (frame_data != nullptr) MHD_websocket_free(ws_stream, frame_data);
            session.close(1002, "Protocol error");
            break;
    }
}

void decode_websocket_buffer(struct MHD_WebSocketStream* ws_stream,
                             websocket_handler* handler,
                             websocket_session& session,
                             const char* buf, size_t buf_len) {
    size_t offset = 0;
    while (offset < buf_len && session.is_valid()) {
        char* frame_data = nullptr;
        size_t frame_len = 0;
        size_t step = 0;
        int status = MHD_websocket_decode(ws_stream,
                                           buf + offset,
                                           buf_len - offset,
                                           &step,
                                           &frame_data,
                                           &frame_len);
        offset += step;
        dispatch_websocket_frame(status, ws_stream, handler, session,
                                 frame_data, frame_len);
        // If decode consumed no bytes, we need more data.
        if (step == 0) break;
    }
}

}  // namespace

std::optional<const char*> websocket_upgrader::validate_websocket_handshake(
        MHD_Connection* connection) {
    const char* connection_header = MHD_lookup_connection_value(connection,
        MHD_HEADER_KIND, MHD_HTTP_HEADER_CONNECTION);
    const char* ws_version = MHD_lookup_connection_value(connection,
        MHD_HEADER_KIND, "Sec-WebSocket-Version");
    const char* ws_key = MHD_lookup_connection_value(connection,
        MHD_HEADER_KIND, "Sec-WebSocket-Key");
    if (connection_header == nullptr
            || strcasestr(connection_header, "Upgrade") == nullptr) {
        return std::nullopt;
    }
    if (ws_version == nullptr || strcmp(ws_version, "13") != 0) {
        return std::nullopt;
    }
    if (ws_key == nullptr || ws_key[0] == '\0') {
        return std::nullopt;
    }
    return ws_key;
}

std::optional<MHD_Result> websocket_upgrader::complete_websocket_upgrade(
        MHD_Connection* connection, detail::connection_context* conn,
        const char* ws_key) {
    // find() returns a shared_ptr copy taken under the registry's read lock,
    // so the handler is kept alive across the MHD upgrade callback even if
    // unregister_ws_resource erases the slot mid-upgrade.
    std::shared_ptr<websocket_handler> handler_sp = ws_.find(conn->standardized_url);
    if (!handler_sp) {
        return std::nullopt;
    }

    // CWE-401: RAII guard so data is freed if MHD_create_response_for_upgrade
    // returns null. Ownership is transferred to MHD (via release()) only after
    // a confirmed successful queue; upgrade_handler receives data_guard.get()
    // as cls and wraps it in unique_ptr for cleanup.
    std::unique_ptr<ws_upgrade_data> data_guard(
        new ws_upgrade_data{std::move(handler_sp)});
    struct MHD_Response* response = MHD_create_response_for_upgrade(
        &websocket_upgrader::upgrade_handler, data_guard.get());
    if (response == nullptr) {
        return std::nullopt;
    }
    MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, "websocket");

    // Compute Sec-WebSocket-Accept from the client's key (RFC 6455 §4.2.2).
    // Base64 of SHA-1 = 28 chars + null. The header is required; a library
    // failure is a fatal handshake error.
    char accept_header[29];
    if (MHD_websocket_create_accept_header(ws_key, accept_header)
            != MHD_WEBSOCKET_STATUS_OK) {
        MHD_destroy_response(response);
        return std::nullopt;
    }
    MHD_add_response_header(response, "Sec-WebSocket-Accept", accept_header);
    MHD_Result to_ret = (MHD_Result) MHD_queue_response(connection,
        MHD_HTTP_SWITCHING_PROTOCOLS, response);
    MHD_destroy_response(response);
    if (to_ret == MHD_YES) {
        // Transfer ownership to MHD only after a confirmed successful queue;
        // if MHD_queue_response returned MHD_NO the upgrade callback will never
        // fire, so data_guard's destructor frees the allocation instead.
        data_guard.release();
    }
    return to_ret;
}

void websocket_upgrader::upgrade_handler(void* cls,
        struct MHD_Connection* connection, void* req_cls, const char* extra_in,
        size_t extra_in_size, MHD_socket sock,
        struct MHD_UpgradeResponseHandle* urh) {
    std::ignore = connection;
    std::ignore = req_cls;

    // Own ws_upgrade_data via unique_ptr for the duration of the session. The
    // shared_ptr<websocket_handler> inside `data` keeps the handler alive
    // across this callback even if a concurrent unregister_ws_resource drops
    // the registration.
    std::unique_ptr<ws_upgrade_data> data(static_cast<ws_upgrade_data*>(cls));
    websocket_handler* handler = data->handler.get();

    struct MHD_WebSocketStream* ws_stream = nullptr;
    int ws_result = MHD_websocket_stream_init(&ws_stream,
        MHD_WEBSOCKET_FLAG_SERVER | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS, 0);
    if (ws_result != MHD_WEBSOCKET_STATUS_OK || ws_stream == nullptr) {
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        return;
    }

    websocket_session session(static_cast<std::uintptr_t>(sock), urh, ws_stream);
    handler->on_open(session);

    // Process any initial data that MHD may have buffered.
    if (extra_in != nullptr && extra_in_size > 0) {
        decode_websocket_buffer(ws_stream, handler, session, extra_in,
                                extra_in_size);
    }

    char buf[4096];
    while (session.is_valid()) {
        ssize_t got = recv(sock, buf, sizeof(buf), 0);
        if (got <= 0) break;
        decode_websocket_buffer(ws_stream, handler, session,
                                buf, static_cast<size_t>(got));
    }
    // Session destructor frees ws_stream and closes urh; `data` (and its
    // shared_ptr handler reference) goes out of scope here.
}

std::optional<MHD_Result> websocket_upgrader::try_handle(
        MHD_Connection* connection, detail::connection_context* conn) {
    const char* upgrade_header = MHD_lookup_connection_value(connection,
        MHD_HEADER_KIND, MHD_HTTP_HEADER_UPGRADE);
    if (upgrade_header == nullptr
            || strcasecmp(upgrade_header, "websocket") != 0) {
        return std::nullopt;
    }
    auto ws_key = validate_websocket_handshake(connection);
    if (!ws_key) {
        // RFC 6455 §4.2.1: required handshake header missing or malformed.
        struct MHD_Response* bad_response = MHD_create_response_from_buffer(
            0, nullptr, MHD_RESPMEM_PERSISTENT);
        MHD_Result ret = (MHD_Result) MHD_queue_response(connection,
            MHD_HTTP_BAD_REQUEST, bad_response);
        MHD_destroy_response(bad_response);
        return ret;
    }
    return complete_websocket_upgrade(connection, conn, *ws_key);
}

}  // namespace detail
}  // namespace httpserver

#endif  // HAVE_WEBSOCKET
