/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/websocket/options.hpp>

namespace httpserver::websocket {

auto options::from_budgets(const server::budget_limits& limits) -> options {
    options out;
    out.max_message_bytes = limits.get(server::resource::ws_message_bytes);
    out.incoming_bytes = limits.get(server::resource::body_buffer_bytes);
    out.output_bytes = limits.get(server::resource::response_queue_bytes);
    return out;
}

auto options::validate() const -> http::outcome {
    const auto ceiling = server::max_capacity(server::resource::ws_message_bytes);
    if (max_message_bytes > ceiling || incoming_bytes < max_message_bytes) return {http::outcome_code::invalid_argument, "invalid incoming message limits"};
    const std::size_t overhead = max_message_bytes < 126 ? 2 : max_message_bytes <= 65535 ? 4 : 10;
    if (output_bytes < max_message_bytes + overhead) return {http::outcome_code::invalid_argument, "output must fit one maximum message"};
    if (incoming_messages == 0 || outgoing_messages == 0) return {http::outcome_code::invalid_argument, "message counts must be positive"};
    return http::outcome::okay();
}

}  // namespace httpserver::websocket
