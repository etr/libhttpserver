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

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINDOWS
#else
#if defined(__CYGWIN__)
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <errno.h>
#include <microhttpd.h>
#ifdef HAVE_WEBSOCKET
#include <microhttpd_ws.h>
#endif  // HAVE_WEBSOCKET
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <algorithm>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/body.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif  // HAVE_GNUTLS

using std::string;
using std::pair;
using std::vector;
using std::map;
using std::set;

namespace httpserver {

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;


#ifdef HAVE_WEBSOCKET
namespace {

// RFC 6455 §5.5.1: a CLOSE frame's payload starts with a 2-byte
// status code (default 1000 "normal closure") followed by an optional
// UTF-8 reason. Pulled out of dispatch_websocket_frame so the switch
// stays under the CCN bar.
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

}  // namespace

static void decode_websocket_buffer(struct MHD_WebSocketStream* ws_stream,
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

namespace detail {

// TASK-050: validate_websocket_handshake / complete_websocket_upgrade moved
// here from webserver_request.cpp to keep that TU under FILE_LOC_MAX
// after adding the after_handler / response_sent firing sites.
std::optional<const char*>
webserver_impl::validate_websocket_handshake(MHD_Connection* connection) {
    const char* connection_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                                MHD_HTTP_HEADER_CONNECTION);
    const char* ws_version = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                         "Sec-WebSocket-Version");
    const char* ws_key = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                     "Sec-WebSocket-Key");
    if (connection_header == nullptr || strcasestr(connection_header, "Upgrade") == nullptr) {
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

std::optional<MHD_Result>
webserver_impl::complete_websocket_upgrade(MHD_Connection* connection,
                                           detail::modded_request* mr,
                                           const char* ws_key) {
    std::shared_lock lock(registered_ws_handlers_mutex_);
    auto ws_it = registered_ws_handlers.find(mr->standardized_url);
    if (ws_it == registered_ws_handlers.end()) {
        return std::nullopt;
    }
    // TASK-035: take a shared_ptr copy under the shared lock so the
    // handler is kept alive across the MHD upgrade callback even if
    // unregister_ws_resource erases the slot mid-upgrade.
    std::shared_ptr<websocket_handler> handler_sp = ws_it->second;
    lock.unlock();

    // CWE-401: RAII guard so data is freed if MHD_create_response_for_upgrade
    // returns null. Ownership is transferred to MHD (via release()) only after
    // the queue call — upgrade_handler receives data_guard.get() as cls and
    // wraps it in unique_ptr for cleanup (see upgrade_handler below).
    std::unique_ptr<ws_upgrade_data> data_guard(
        new ws_upgrade_data{this, std::move(handler_sp)});
    struct MHD_Response* response = MHD_create_response_for_upgrade(
        &webserver_impl::upgrade_handler, data_guard.get());
    if (response == nullptr) {
        return std::nullopt;
    }
    MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, "websocket");

    // Compute Sec-WebSocket-Accept from client's key (RFC 6455 §4.2.2).
    // Base64 of SHA-1 = 28 chars + null.
    // RFC 6455 §4.2.2: the Sec-WebSocket-Accept header is required; if the
    // library call fails treat it as a fatal handshake error and abort.
    char accept_header[29];
    if (MHD_websocket_create_accept_header(ws_key, accept_header) != MHD_WEBSOCKET_STATUS_OK) {
        MHD_destroy_response(response);
        return std::nullopt;
    }
    MHD_add_response_header(response, "Sec-WebSocket-Accept", accept_header);
    MHD_Result to_ret = (MHD_Result) MHD_queue_response(connection,
                                                       MHD_HTTP_SWITCHING_PROTOCOLS,
                                                       response);
    MHD_destroy_response(response);
    if (to_ret == MHD_YES) {
        // Transfer ownership to MHD: upgrade_handler receives data_guard.get()
        // as cls and wraps it in unique_ptr for cleanup. Only release after a
        // confirmed successful queue; if MHD_queue_response returns MHD_NO the
        // upgrade callback will never fire, so data_guard's destructor frees the
        // allocation instead.
        data_guard.release();
    }
    return to_ret;
}

void webserver_impl::upgrade_handler(void *cls, struct MHD_Connection* connection,
                                void *req_cls, const char *extra_in,
                                size_t extra_in_size, MHD_socket sock,
                                struct MHD_UpgradeResponseHandle *urh) {
    std::ignore = connection;
    std::ignore = req_cls;

    // TASK-035: own ws_upgrade_data via unique_ptr for the duration of
    // the session. The shared_ptr<websocket_handler> inside `data`
    // keeps the handler alive across this upgrade callback even if a
    // concurrent unregister_ws_resource drops the registration in the
    // owning webserver.
    std::unique_ptr<ws_upgrade_data> data(static_cast<ws_upgrade_data*>(cls));
    websocket_handler* handler = data->handler.get();

    // Create a WebSocket stream for this connection
    struct MHD_WebSocketStream* ws_stream = nullptr;
    int ws_result = MHD_websocket_stream_init(&ws_stream,
                                               MHD_WEBSOCKET_FLAG_SERVER | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS,
                                               0);
    if (ws_result != MHD_WEBSOCKET_STATUS_OK || ws_stream == nullptr) {
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        return;
    }

    websocket_session session(static_cast<std::uintptr_t>(sock), urh, ws_stream);
    handler->on_open(session);

    // Process any initial data that MHD may have buffered
    if (extra_in != nullptr && extra_in_size > 0) {
        decode_websocket_buffer(ws_stream, handler, session, extra_in, extra_in_size);
    }

    // Receive loop
    char buf[4096];
    while (session.is_valid()) {
        ssize_t got = recv(sock, buf, sizeof(buf), 0);
        if (got <= 0) break;

        decode_websocket_buffer(ws_stream, handler, session,
                                buf, static_cast<size_t>(got));
    }

    // Session destructor will free ws_stream and close urh.
    // `data` (and its shared_ptr handler reference) goes out of scope here.
}

}  // namespace detail
#endif  // HAVE_WEBSOCKET

// TASK-050: try_handle_websocket_upgrade lives outside the HAVE_WEBSOCKET
// guard because it has a no-op definition on the WS-off branch. Moved
// here from webserver_request.cpp.
namespace detail {
std::optional<MHD_Result>
webserver_impl::try_handle_websocket_upgrade(MHD_Connection* connection,
                                             detail::modded_request* mr) {
#ifdef HAVE_WEBSOCKET
    const char* upgrade_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                             MHD_HTTP_HEADER_UPGRADE);
    if (upgrade_header == nullptr || strcasecmp(upgrade_header, "websocket") != 0) {
        return std::nullopt;
    }
    auto ws_key = validate_websocket_handshake(connection);
    if (!ws_key) {
        // RFC 6455 §4.2.1: required handshake header missing or malformed.
        struct MHD_Response* bad_response = MHD_create_response_from_buffer(0, nullptr,
                                                                            MHD_RESPMEM_PERSISTENT);
        MHD_Result ret = (MHD_Result) MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST,
                                                         bad_response);
        MHD_destroy_response(bad_response);
        return ret;
    }
    return complete_websocket_upgrade(connection, mr, *ws_key);
#else
    (void)connection;
    (void)mr;
    return std::nullopt;
#endif  // HAVE_WEBSOCKET
}
}  // namespace detail

}  // namespace httpserver
