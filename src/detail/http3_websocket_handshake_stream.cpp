/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <new>
#include <memory>
#include <stdexcept>
#include <utility>
#include <httpserver/detail/http3_request_state.hpp>
#include <httpserver/detail/http3_websocket_handshake.hpp>
namespace httpserver::detail {
websocket_upgrade_result http3_request_engine::state::stream::on_upgrade(const ws_upgrade_options& options) {
    if (owner.native.workers) {
        websocket_upgrade_result out;
        out.status = {http::outcome_code::not_supported, "Native HTTP/3 WebSocket upgrade is unavailable"};
        return out;
    }
    try {
        return owner.upgrade(*this, options);
    } catch (const std::bad_alloc&) {
        websocket_upgrade_result out;
        out.status = {http::outcome_code::limit_exceeded, "WebSocket allocation refused"}; return out;
    } catch (const std::length_error&) {
        websocket_upgrade_result out;
        out.status = {http::outcome_code::limit_exceeded, "WebSocket allocation size refused"}; return out;
    }
}
bool http3_request_engine::state::upgrade_available(const stream& value) const {
    const auto& r = value.transport;
    return connection.connect_eligible(r.transport->id()) && !disconnected && phase == drain_phase::running && !r.cancelled && !r.abandoned &&
        !r.receive_done && !r.terminal && !r.transport->final_size() && !value.body.receive_ended();
}
websocket_upgrade_result http3_request_engine::state::upgrade(stream& value, const ws_upgrade_options& options) {
    websocket_upgrade_result out;
    auto plan = negotiate_http3_websocket(value.request.head(), value.connect, options, limits.framing.headers.max_expanded_bytes, limits.framing.headers.max_fields);
    out.status = plan.status; out.rejection_status = plan.rejection_status; out.rejection_fields = std::move(plan.rejection_fields);
    if (!out.status.ok()) return out;
    auto& r = value.transport;
    if (!upgrade_available(value)) {
        out.status = {http::outcome_code::connection_closed, "WebSocket stream unavailable"}; return out;
    }
    http::fields fields;
    if (!plan.selected_subprotocol.empty()) fields.append("sec-websocket-protocol", plan.selected_subprotocol);
    auto effective = options.limits;
    server::reservation input, output, message;
    if (!reserve_websocket(effective, input, output, message)) {
        out.status = {http::outcome_code::limit_exceeded, "WebSocket storage admission refused"}; return out;
    }
    auto driver = std::make_shared<websocket_driver>(effective);
    auto session = driver->take_session();
    observe_websocket(*driver, r.transport->id());
    // Only this section commits the opening decision; ordinary respond() would
    // abandon the receive half.
    if (!section(r, 200, fields, false)) {
        out.status = {http::outcome_code::limit_exceeded, "WebSocket response admission refused"}; return out;
    }
    value.websocket = std::move(driver);
    value.websocket_input_charge = std::move(input);
    value.websocket_output_charge = std::move(output);
    value.websocket_message_charge = std::move(message);
    out.selected_subprotocol = std::move(plan.selected_subprotocol);
    out.session.emplace(std::move(session));
    return out;
}
bool http3_request_engine::state::reserve_websocket(websocket::options& options, server::reservation& input_charge,
        server::reservation& output_charge, server::reservation& message_charge) {
    const auto& cap = limits.websocket;
    auto budget = data.ancestors;
    options.incoming_messages = std::min(options.incoming_messages, cap.incoming_messages);
    options.outgoing_messages = std::min(options.outgoing_messages, cap.outgoing_messages);
    const auto input_fixed = 4096 + 256 * options.incoming_messages;
    // Independently rounded byte deques, frame/container metadata, and three
    // control frames use the same reservation envelope as HTTP/2.
    const auto output_fixed = 4096 + (8192 + 256) * (options.outgoing_messages + 3);
    auto free = [&](server::resource kind) { return budget.capacity(kind) - budget.in_use(kind); };
    if (free(server::resource::body_buffer_bytes) <= input_fixed || free(server::resource::response_queue_bytes) <= output_fixed + 40) return false;
    options.incoming_bytes = std::min({options.incoming_bytes, cap.incoming_bytes, (free(server::resource::body_buffer_bytes) - input_fixed) / 5});
    options.output_bytes = std::min({options.output_bytes, cap.output_bytes, (free(server::resource::response_queue_bytes) - output_fixed) / 4});
    if (options.output_bytes <= 10) return false;
    options.max_message_bytes = std::min({options.max_message_bytes, cap.max_message_bytes, options.incoming_bytes,
        options.output_bytes - 10, free(server::resource::ws_message_bytes)});
    if (!options.max_message_bytes || !options.validate().ok()) return false;
    return budget.reserve(server::resource::body_buffer_bytes, 5 * options.incoming_bytes + input_fixed, input_charge).ok() &&
        budget.reserve(server::resource::response_queue_bytes, 4 * options.output_bytes + output_fixed, output_charge).ok() &&
        budget.reserve(server::resource::ws_message_bytes, options.max_message_bytes, message_charge).ok();
}
}  // namespace httpserver::detail
