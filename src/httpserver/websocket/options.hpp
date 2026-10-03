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

#ifndef SRC_HTTPSERVER_WEBSOCKET_OPTIONS_HPP_
#define SRC_HTTPSERVER_WEBSOCKET_OPTIONS_HPP_
#include <cstddef>
#include <limits>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/budgets.hpp>
namespace httpserver::websocket {
struct options {
    std::size_t max_message_bytes = 1048576;
    std::size_t incoming_bytes = 8388608;
    std::size_t output_bytes = 4194304;
    std::size_t incoming_messages = 64;
    std::size_t outgoing_messages = 64;

    // Counts bound empty-message traffic and queue metadata independently
    // of byte limits. Control output has a separate fixed reservation:
    // one active control, one pending Pong and one pending Close.
    static options from_budgets(const server::budget_limits& limits) {
        options out;
        out.max_message_bytes = limits.get(server::resource::ws_message_bytes);
        out.incoming_bytes = limits.get(server::resource::body_buffer_bytes);
        out.output_bytes = limits.get(server::resource::response_queue_bytes);
        return out;
    }
    http::outcome validate() const {
        const auto ceiling = server::max_capacity(server::resource::ws_message_bytes);
        if (max_message_bytes > ceiling || incoming_bytes < max_message_bytes)
            return {http::outcome_code::invalid_argument, "invalid incoming message limits"};
        const std::size_t overhead = max_message_bytes < 126 ? 2 : max_message_bytes <= 65535 ? 4 : 10;
        if (output_bytes < max_message_bytes + overhead)
            return {http::outcome_code::invalid_argument, "output must fit one maximum message"};
        if (incoming_messages == 0 || outgoing_messages == 0)
            return {http::outcome_code::invalid_argument, "message counts must be positive"};
        return http::outcome::okay();
    }
};
}  // namespace httpserver::websocket
#endif  // SRC_HTTPSERVER_WEBSOCKET_OPTIONS_HPP_
