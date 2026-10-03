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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#ifndef SRC_HTTPSERVER_WEBSOCKET_MESSAGE_HPP_
#define SRC_HTTPSERVER_WEBSOCKET_MESSAGE_HPP_
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <httpserver/http/outcome.hpp>
namespace httpserver::websocket {
enum class message_kind : std::uint8_t { text = 1, binary = 2 };
struct message {
    message_kind kind = message_kind::binary;
    std::vector<std::byte> data;
};
enum class send_disposition : std::uint8_t { accepted, backpressured, closed, rejected };
struct send_result {
    http::outcome status;
    send_disposition disposition = send_disposition::rejected;
};
struct receive_result {
    http::outcome status;
    std::optional<message> value;
};
// Absent code represents an absent peer status or abnormal termination.
// Reserved diagnostic statuses are never emitted in a Close frame.
struct close_info {
    http::outcome status;
    std::optional<std::uint16_t> code;
    std::string reason;
    bool clean = false;
};
struct feed_result {
    http::outcome status;
    std::size_t consumed = 0;
    bool blocked = false;
};
struct queue_usage {
    std::size_t incoming_bytes = 0;
    std::size_t incoming_messages = 0;
    std::size_t output_bytes = 0;
    std::size_t outgoing_messages = 0;
};
}  // namespace httpserver::websocket
#endif  // SRC_HTTPSERVER_WEBSOCKET_MESSAGE_HPP_
