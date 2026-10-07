/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_request_state.hpp>
namespace httpserver::detail {
void http2_request_engine::state::sync_settings() {
    const auto peer = connection.peer_settings().initial_window_size;
    const auto local = connection.acknowledged_window();
    for (auto& [id, value] : streams) {
        auto& body = value->body;
        if (connection.peer_window_peak() > peer_initial &&
            body.send_window.available + connection.peer_window_peak() - peer_initial > 0x7fffffff) {
            fail(connection_error(http2_error_code::flow_control_error)); return;
        }
        if (!body.send_window.adjust(static_cast<std::int64_t>(peer) - peer_initial)) {
            fail(connection_error(http2_error_code::flow_control_error)); return;
        }
        body.receive_window.adjust(static_cast<std::int64_t>(local) - local_initial);
    }
    peer_initial = peer; local_initial = local;
}
void http2_request_engine::state::publish_stream_credit(stream& value) {
    auto& body = value.body;
    consumed += body.consumed; body.consumed = 0;
    if (local_initial || !body.admitted() || body.receive_ended() || value.reset_pending) return;
    const auto target = value.websocket_input_blocked ? body.receive_window.available + value.credit_queued + value.padding_credit :
        static_cast<std::int64_t>(body.receive_capacity() - body.unread());
    const auto gap = target - body.receive_window.available - value.credit_queued;
    if (gap > 0 && gap <= 0x7fffffff && connection.queue_window_update(value.id, gap) == http::outcome_code::ok) {
        value.credit_queued += gap; value.padding_credit = 0;
    }
}
void http2_request_engine::state::publish_credit() {
    if (connection.failure()) return;
    for (auto& [id, value] : streams) publish_stream_credit(*value);
    if (consumed && connection.queue_window_update(0, consumed) == http::outcome_code::ok) {
        connection_credit_queued += consumed; consumed = 0;
    }
}
void http2_request_engine::state::expose_credit(std::span<const std::uint8_t> control) {
    if (control.size() < 13 || control[3] != 8) return;
    const auto id = (std::uint32_t{control[5]} << 24) | (control[6] << 16) | (control[7] << 8) | control[8];
    const auto n = (std::uint32_t{control[9]} << 24) | (control[10] << 16) | (control[11] << 8) | control[12];
    if (!id) {
        receive_window.increase(n); connection_credit_queued -= n;
    } else if (auto found = streams.find(id); found != streams.end()) {
        found->second->body.receive_window.increase(n); found->second->credit_queued -= n;
    }
}
std::optional<http2_error> http2_request_engine::state::update_window() {
    const auto h = connection.header(); const auto payload = connection.payload();
    const auto n = ((std::uint32_t{payload[0]} << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3]) & 0x7fffffff;
    if (!h.stream_id) {
        if (!send_window.increase(n)) return connection_error(http2_error_code::flow_control_error);
    } else if (auto found = streams.find(h.stream_id); found != streams.end()) {
        if (!found->second->body.send_window.increase(n)) reset(h.stream_id, http2_error_code::flow_control_error);
    } else if (h.stream_id > last_stream) {
        return connection_error(http2_error_code::protocol_error);
    }
    return {};
}
std::optional<http2_error> http2_request_engine::state::data_frame() {
    const auto h = connection.header();
    if (!receive_window.debit_data(h.length, h.flags & 1)) return connection_error(http2_error_code::flow_control_error);
    auto found = streams.find(h.stream_id);
    if (found == streams.end()) {
        consumed += h.length;
        if (h.stream_id > last_stream) return connection_error(http2_error_code::protocol_error);
        const auto closed = closed_reason(h.stream_id);
        if (closed != closed_kind::local_reset) return connection_error(http2_error_code::stream_closed);
        return {};
    }
    auto& value = *found->second; auto& body = value.body;
    if (body.receive_ended()) {
        consumed += h.length; reset(h.stream_id, http2_error_code::stream_closed); return {};
    }
    if (!body.receive_window.debit_data(h.length, h.flags & 1)) {
        consumed += h.length; reset(h.stream_id, http2_error_code::flow_control_error); return {};
    }
    auto payload = connection.payload(); const auto full = payload.size();
    if (h.flags & 8) payload = payload.subspan(1, payload.size() - payload.front() - 1);
    const auto padding = full - payload.size();
    consumed += padding;
    if (value.websocket) value.padding_credit += padding;
    receive_data(value, payload, h.flags & 1);
    return {};
}
void http2_request_engine::state::receive_data(stream& value, std::span<const std::uint8_t> payload, bool ended) {
    auto& body = value.body;
    const auto before = body.unread();
    if (!body.receive(payload, ended)) {
        consumed += payload.size() - (body.unread() - before);
        reset(value.id, http2_error_code::protocol_error);
    }
    if (value.websocket && !value.reset_pending) pump_websocket_input(value);
}

bool http2_request_engine::state::trailers(std::uint32_t id, bool ended, const std::vector<hpack_field>& fields) {
    auto& value = *streams.at(id);
    if (!value.http_trailers_allowed(ended)) {
        reset(id, value.body.receive_ended() ? http2_error_code::stream_closed : http2_error_code::protocol_error);
        return false;
    }
    std::size_t size = 0;
    for (const auto& field : fields) {
        if (!http2_trailer_field(field.name, field.value)) {
            reset(id, http2_error_code::protocol_error);
            return false;
        }
        size += field.name.size() + field.value.size() + 32;
    }
    if (!reserve(server::resource::header_bytes, size * head_copy_allowance, value.trailer_charge) ||
        !reserve(server::resource::header_fields, fields.size(), value.trailer_fields_charge)) { reset(id, http2_error_code::refused_stream); return false; }
    http::fields trailers;
    for (const auto& field : fields) trailers.append(field.name, field.value);
    if (!value.body.end_receive(std::move(trailers))) {
        reset(id, http2_error_code::protocol_error);
        return false;
    }
    return true;
}
bool http2_request_engine::state::data_ready(stream& value) {
    return value.headers_sent && value.response_started && !value.send_ended && !value.reset_pending;
}
bool http2_request_engine::state::frame_data(stream& value) {
    if (value.websocket) return frame_websocket_data(value);
    auto& body = value.body;
    const bool end = body.finished() && !body.queued();
    if (end && !body.sent_trailers().empty()) return encode_trailers(value);
    const auto window = std::max<std::int64_t>(0, std::min(send_window.available, body.send_window.available));
    const auto n = std::min<std::size_t>({body.queued(), data_quantum, connection.peer_settings().max_frame_size, static_cast<std::size_t>(window)});
    if (!n && !end) return false;
    if (!reserve(server::resource::response_queue_bytes, 2 * n + 9 + 128, active_charge)) {
        fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); return false;
    }
    auto bytes = body.send_prefix(n);
    active.reserve(n + 9); append_frame(active, 0, end ? 1 : 0, value.id, bytes);
    send_window.debit(n); body.send_window.debit(n);
    active_used = 0; active_stream = selected = value.id; active_body = n; active_end = end;
    return true;
}
bool http2_request_engine::state::encode_data() {
    if (connection.failure() || streams.empty()) return false;
    auto start = streams.upper_bound(selected);
    for (std::size_t attempts = 0; attempts < streams.size(); ++attempts) {
        if (start == streams.end()) start = streams.begin();
        auto& value = *start->second; ++start;
        if (data_ready(value) && frame_data(value)) return true;
    }
    return false;
}
}  // namespace httpserver::detail
