/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_request_state.hpp>
namespace httpserver::detail {
std::optional<std::size_t> http2_request_engine::state::response_size(std::uint16_t status, const http::fields& fields) const {
    if (status < 200 || status > 599 || fields.size() >= limits.headers.max_fields) return {};
    std::size_t expanded = 42;
    for (auto field : fields.entries()) {
        const auto size = field.name.size() + field.value.size() + 32;
        if (size > limits.headers.max_expanded_bytes || expanded > limits.headers.max_expanded_bytes - size) return {};
        expanded += size;
    }
    return expanded;
}
bool http2_request_engine::state::response_fields(response& value, std::uint16_t status, const http::fields& fields) {
    value.fields.reserve(fields.size() + 1);
    value.fields.push_back({":status", std::to_string(status), hpack_indexing::without_indexing});
    for (auto field : fields.entries()) {
        std::string name(field.name);
        for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!http2_regular_field(name, field.value)) return false;
        value.fields.push_back({std::move(name), std::string(field.value), hpack_indexing::without_indexing});
    }
    return true;
}
bool http2_request_engine::state::prepare_response(stream& value, std::uint16_t status, const http::fields& fields, bool streaming) {
    const bool metadata = status == 304 || value.head.request_method.id() == http::method_id::head;
    std::optional<std::uint64_t> length;
    if (!http2_content_length(fields, length)) return false;
    if (!streaming && !valid_response_lengths(fields, metadata)) return false;
    const bool no_content = status >= 204 && status <= 205;
    if (no_content && length.value_or(0)) return false;
    const bool forbidden = metadata || no_content;
    value.response_started = streaming; value.send_ended = !streaming;
    return !streaming || value.body.prepare_send(limits.response_buffer_bytes, length, forbidden);
}
bool http2_request_engine::state::reserve_response(response& value, std::size_t expanded, std::size_t fields) {
    // Bound semantic fields plus peak HPACK allocations and framed wire before
    // encoding; literals never insert into the encoder's dynamic table.
    value.wire_limit = expanded + 32;
    value.framed_limit = value.wire_limit + (value.wire_limit / 16384 + 1) * 9;
    const auto semantic = expanded + (fields + 1) * (sizeof(hpack_field) + 64);
    return reserve(server::resource::response_queue_bytes,
        semantic + 5 * value.wire_limit + value.framed_limit + sizeof(response) + 256, value.charge);
}
void http2_request_engine::state::respond(std::uint32_t id, std::uint16_t status, const http::fields& fields, bool streaming) {
    if (connection.failure()) return;
    const auto found = streams.find(id);
    if (found == streams.end() || found->second->reset_pending) return;
    const auto expanded = response_size(status, fields);
    if (!expanded || !prepare_response(*found->second, status, fields, streaming)) {
        reset(id, http2_error_code::internal_error); return;
    }
    if (!queue_room()) return;
    response value; value.stream = id; value.ended = !streaming;
    if (!reserve_response(value, *expanded, fields.size())) {
        fail(connection_error(http2_error_code::enhance_your_calm, http::outcome_code::limit_exceeded)); return;
    }
    if (!response_fields(value, status, fields)) {
        reset(id, http2_error_code::internal_error);
        return;
    }
    pending.push_back(std::move(value));
}
bool http2_request_engine::state::encode_next() {
    auto value = std::move(pending.front()); pending.pop_front();
    active.clear(); active_used = 0;
    active_charge = std::move(value.charge);
    if (value.reset) {
        active.reserve(value.framed_limit);
        auto n = static_cast<std::uint32_t>(*value.reset);
        std::array<std::uint8_t, 4> code{static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)};
        append_frame(active, 3, 0, value.stream, code); return true;
    }
    auto maximum = connection.peer_settings().max_header_list_size;
    auto expanded = limits.headers.max_expanded_bytes;
    if (maximum) expanded = std::min(expanded, static_cast<std::size_t>(*maximum));
    std::size_t size = 0;
    for (const auto& field : value.fields) size += field.name.size() + field.value.size() + 32;
    if (size > expanded) {
        std::vector<hpack_field>().swap(value.fields);
        active_charge.release();
        reset(value.stream, http2_error_code::internal_error);
        return false;
    }
    active.reserve(value.framed_limit);
    auto encoded = connection.compression().encoder().encode_section(value.fields, {value.wire_limit, expanded, limits.headers.max_fields});
    if (!encoded.status.ok()) {
        std::vector<hpack_field>().swap(value.fields);
        std::vector<std::uint8_t>().swap(active);
        active_charge.release();
        fail(connection_error(http2_error_code::internal_error)); return false;
    }
    frame_headers(value, hpack_octets(encoded.value));
    if (auto found = streams.find(value.stream); found != streams.end()) {
        found->second->headers_sent = true;
    }
    return true;
}
void http2_request_engine::state::frame_headers(const response& value, std::span<const std::uint8_t> bytes) {
    const auto frame_size = connection.peer_settings().max_frame_size;
    bool initial = true;
    do {
        auto n = std::min(bytes.size(), static_cast<std::size_t>(frame_size));
        auto flags = static_cast<std::uint8_t>((initial && value.ended ? 1 : 0) | (bytes.size() == n ? 4 : 0));
        append_frame(active, initial ? 1 : 9, flags, value.stream, bytes.first(n));
        bytes = bytes.subspan(n); initial = false;
    } while (!bytes.empty());
}
bool http2_request_engine::state::encode_trailers(stream& stream) {
    const auto& fields = stream.body.sent_trailers();
    std::size_t expanded = 0;
    for (auto field : fields.entries()) expanded += field.name.size() + field.value.size() + 32;
    if (expanded > limits.headers.max_expanded_bytes || fields.size() > limits.headers.max_fields) {
        reset(stream.id, http2_error_code::internal_error); return false;
    }
    response value; value.stream = stream.id; value.wire_limit = expanded + 32;
    value.framed_limit = value.wire_limit + (value.wire_limit / 16384 + 1) * 9;
    if (!reserve(server::resource::response_queue_bytes, 8 * value.wire_limit + fields.size() * sizeof(hpack_field) + 256, value.charge)) {
        reset(stream.id, http2_error_code::internal_error); return false;
    }
    value.fields.reserve(fields.size());
    for (auto field : fields.entries()) value.fields.push_back({std::string(field.name), std::string(field.value), hpack_indexing::without_indexing});
    pending.push_front(std::move(value));
    if (!encode_next()) return false;
    active_stream = selected = stream.id; active_end = true;
    return true;
}
}  // namespace httpserver::detail
