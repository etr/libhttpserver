/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/http3_request_state.hpp>
namespace httpserver::detail {
namespace {
void integer(std::vector<std::byte>& out, std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    auto encoded = encode_quic_varint(value, bytes);
    out.insert(out.end(), bytes.begin(), bytes.begin() + encoded.consumed);
}
std::string lower(std::string_view input) {
    std::string out(input);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
bool response_length(const http::fields& fields, std::optional<std::uint64_t>& length) {
    http::fields normalized;
    for (auto field : fields.entries())
        if (lower(field.name) == "content-length") normalized.append("content-length", field.value);
    return http3_content_length(normalized, length);
}
quic_information_result retain_prefix(quic_recovery& recovery, quic_stream_frame& frame, quic_stream_retention retention) {
    for (;;) {
        auto result = recovery.retain_stream(frame, retention);
        if (result || frame.data.size() <= 1) return result;
        if (result.code != quic_recovery_code::capacity && result.code != quic_recovery_code::no_memory) return result;
        // Refusal admits no bytes. Halving bounds retries by the prefix width.
        frame.data = frame.data.first(frame.data.size() / 2);
        frame.fin = false;
    }
}
bool retention_available(bool local, std::size_t count, std::size_t cap, bool pending) {
    return count < cap && (!local || !pending);
}
bool no_content(std::uint16_t status) {
    return status == 204 || status == 205;
}
bool empty_length_allowed(std::uint16_t status, bool metadata, bool streaming, std::optional<std::uint64_t> length) {
    if (no_content(status) && length.value_or(0)) return false;
    return streaming || metadata || !length.value_or(0);
}
bool response_policy(std::uint16_t status, const http::fields& fields, http::method_id method, bool streaming, std::optional<std::uint64_t>& length, bool& forbidden) {
    if (status < 200 || status > 599 || !response_length(fields, length)) return false;
    const bool metadata = status == 304 || method == http::method_id::head;
    if (!empty_length_allowed(status, metadata, streaming, length)) return false;
    forbidden = metadata || no_content(status);
    return true;
}
std::optional<std::size_t> field_size(const http::fields& fields, qpack_section_limits cap, std::uint64_t peer, bool trailers) {
    const auto limit = std::min<std::uint64_t>(cap.max_expanded_bytes, peer);
    auto remaining = static_cast<std::size_t>(limit);
    if (fields.size() + (trailers ? 0 : 1) > cap.max_fields) return {};
    if (!trailers && !qpack_admit_field(7, 3, remaining)) return {};
    for (auto field : fields.entries())
        if (!qpack_admit_field(field.name.size(), field.value.size(), remaining)) return {};
    return limit - remaining;
}
bool append_fields(std::vector<qpack_field>& values, const http::fields& fields, bool trailers) {
    for (auto field : fields.entries()) {
        auto name = lower(field.name);
        const bool valid = trailers ? http3_trailer_field(name, field.value) : http3_regular_field(name, field.value);
        if (!valid) return false;
        values.push_back({std::move(name), std::string(field.value), true});
    }
    return true;
}
}  // namespace
bool http3_request_engine::state::reserve_trailers(stream& s, const http::fields& fields) {
    const auto size = field_size(fields, limits.framing.headers, connection.peer_settings().max_field_section, true);
    if (!size) return false;
    const auto bytes = 4 * *size + fields.size() * 128 + 256;
    return data.budget.reserve(server::resource::quic_reassembly_bytes, bytes, s.sender_trailer_charge).ok();
}
bool http3_request_engine::state::section(record& r, std::uint16_t status, const http::fields& fields, bool fin, bool trailers) {
    auto cap = limits.framing.headers;
    const auto size = field_size(fields, cap, connection.peer_settings().max_field_section, trailers);
    if (!size || r.sections.size() >= 2) return false;
    const auto count = fields.size() + (trailers ? 0 : 1);
    http3_request_helpers::wire_section section;
    const auto peak = 6 * (*size + 32) + count * (sizeof(qpack_field) + 128) + 256;
    if (!data.budget.reserve(server::resource::quic_reassembly_bytes, peak, section.charge).ok()) return false;
    std::vector<qpack_field> values;
    values.reserve(count);
    if (!trailers) values.push_back({":status", std::to_string(status)});
    if (!append_fields(values, fields, trailers)) return false;
    std::vector<qpack_field_view> views;
    views.reserve(values.size());
    for (const auto& field : values) views.push_back(field.view());
    auto encoded = connection.encode_response_section(r.transport->id(), views);
    if (!encoded.status.ok()) return false;
    try {
        integer(section.bytes, 1);
        integer(section.bytes, encoded.value.size());
        auto bytes = std::as_bytes(std::span(encoded.value));
        section.bytes.insert(section.bytes.end(), bytes.begin(), bytes.end());
        section.fin = fin;
        section.token = encoded.token;
        r.sections.push_back(std::move(section));
    } catch (...) {
        connection.cancel_section(encoded.token);
        throw;
    }
    return true;
}
void http3_request_engine::state::respond(record& r, std::uint16_t status, const http::fields& fields, bool streaming) {
    if (r.cancelled || disconnected || !r.semantic) return;
    auto& s = *r.semantic;
    std::optional<std::uint64_t> length;
    bool forbidden = false;
    if (!response_policy(status, fields, s.request.head().request_method.id(), streaming, length, forbidden)) {
        reset(r, 0x102);
        return;
    }
    if (!section(r, status, fields, !streaming)) {
        reset(r, 0x102);
        return;
    }
    s.streaming = streaming;
    if (streaming && !s.body.prepare_send(limits.response_buffer_bytes, length, forbidden)) {
        reset(r, 0x107);
        return;
    }
    abandon(r);
}
bool http3_request_engine::state::stream_output(record& r) {
    if (!r.semantic || !r.semantic->streaming) return false;
    auto& body = r.semantic->body;
    if (body.queued()) {
        const auto n = std::min(body.queued(), limits.response_buffer_bytes);
        integer(r.output, 0);
        integer(r.output, n);
        r.output_body_begin = r.output.size();
        auto payload = body.send_prefix(n);
        r.output.insert(r.output.end(), payload.begin(), payload.end());
        return true;
    }
    if (!body.finished()) return false;
    if (!r.output_trailers && !body.sent_trailers().empty()) {
        if (!section(r, 0, body.sent_trailers(), true, true)) {
            reset(r, 0x102);
            return false;
        }
        r.output_trailers = true;
        return prepare_output(r);
    }
    r.output_fin = true;
    return true;
}
bool http3_request_engine::state::websocket_output(record& r) {
    if (!r.semantic || !r.semantic->websocket) return false;
    auto driver = r.semantic->websocket;
    const auto progress = driver->snapshot();
    websocket_terminal(r);
    if (r.cancelled) return false;
    if (progress.close_sent && !progress.output_pending) {
        r.output_fin = true;
        return true;
    }
    if (!progress.output_pending) return false;
    std::vector<std::byte> payload(limits.response_buffer_bytes);
    const auto n = driver->copy_output(payload);
    if (!n) return false;
    integer(r.output, 0);
    integer(r.output, n);
    r.output_body_begin = r.output.size();
    r.output.insert(r.output.end(), payload.begin(), payload.begin() + n);
    return true;
}
bool http3_request_engine::state::prepare_output(record& r) {
    if (!r.output.empty() || r.output_fin) return true;
    if (r.sections.empty()) return r.semantic && r.semantic->websocket ? websocket_output(r) : stream_output(r);
    auto& section = r.sections.front();
    r.output = std::move(section.bytes);
    r.output_charge = std::move(section.charge);
    r.output_fin = section.fin;
    r.output_token = section.token;
    r.sections.pop_front();
    return true;
}
bool http3_request_engine::state::submit(record& r, std::span<const std::byte> bytes, bool fin) {
    auto& count = r.local ? retained_critical : retained;
    const auto cap = r.local ? critical_output_records : limits.max_pending_output_records;
    if (!retention_available(r.local, count, cap, !r.information.empty())) return false;
    auto allowance = flow.send_allowance(r.transport->id(), r.retained_offset, bytes.size());
    if (!allowance || (!allowance.bytes && !bytes.empty())) return false;
    auto candidate = bytes.first(allowance.bytes);
    fin = fin && candidate.size() == bytes.size();
    const auto retention = r.local ? quic_stream_retention::critical : quic_stream_retention::ordinary;
    quic_stream_frame frame{r.transport->id(), r.retained_offset, candidate, fin};
    auto result = retain_prefix(recovery, frame, retention);
    if (!result) return false;
    r.information.push_back(result.id);
    ++count;
    r.retained_offset += frame.data.size();
    r.fin_retained = frame.fin;
    return true;
}
bool http3_request_engine::state::local_output(record& r) {
    auto bytes = connection.local_prefix(r.local_role);
    if (bytes.empty()) return false;
    const auto before = r.retained_offset;
    if (!submit(r, bytes, false)) return false;
    connection.advance_local_prefix(r.local_role, r.retained_offset - before);
    return true;
}
void http3_request_engine::state::retire_output(record& r, std::size_t start) {
    if (r.output_body_begin && r.semantic) {
        const auto from = std::max(start, r.output_body_begin), to = std::max(r.output_position, r.output_body_begin);
        if (to > from) {
            auto driver = r.semantic->websocket;
            if (driver) driver->consume_output(to - from);
            else r.semantic->body.retire(to - from);
        }
    }
    if (r.cancelled) return;
    if (r.output_position == r.output.size()) {
        std::vector<std::byte>().swap(r.output);
        r.output_charge.release();
        r.output_position = r.output_body_begin = 0;
        r.output_fin = false;
    }
}
bool http3_request_engine::state::output(record& r) {
    if (r.cancelled || r.fin_retained) return false;
    if (r.local) return local_output(r);
    if (!prepare_output(r) || r.cancelled) return false;
    reserve_critical_output();
    const auto before = r.retained_offset;
    const auto start = r.output_position;
    if (!submit(r, std::span(r.output).subspan(start), r.output_fin)) return false;
    connection.publish_section(r.output_token);
    r.output_token = 0;
    r.output_position += r.retained_offset - before;
    retire_output(r, start);
    return true;
}

void http3_request_engine::state::ordinary_output() {
    auto it = ordinary_after ? records.upper_bound(*ordinary_after) : records.begin();
    for (std::size_t n = 0; n < records.size(); ++n) {
        if (it == records.end()) it = records.begin();
        auto& [id, r] = *it++;
        if (!r->local && output(*r)) ordinary_after = id;
    }
}

void http3_request_engine::state::reserve_critical_output() {
    for (std::size_t n = 0; n < critical_records.size(); ++n) {
        auto* r = critical_records[(critical_next + n) % critical_records.size()];
        if (!r || r->cancelled) continue;
        const auto id = r->transport->id();
        const auto bytes = connection.local_prefix(r->local_role).size();
        auto allowance = flow.send_allowance(id, r->retained_offset, bytes);
        if (allowance) flow.reserve_critical(id, r->retained_offset, allowance.bytes);
    }
}
void http3_request_engine::state::critical_output() {
    const auto start = critical_next;
    for (std::size_t n = 0; n < critical_records.size(); ++n) {
        const auto slot = (start + n) % critical_records.size();
        auto* r = critical_records[slot];
        if (r && output(*r)) critical_next = (slot + 1) % critical_records.size();
    }
}

http3_progress http3_request_engine::pump_output() {
    auto s = state_;
    state::pump_guard guard(*s);
    if (s->error) return http3_progress::failed;
    if (s->disconnected) return http3_progress::input;
    try {
        s->receive_pending();
        s->reserve_critical_output();
        s->critical_output();
        s->ordinary_output();
    } catch (const std::bad_alloc&) {
        s->fail(h3_error(0x107, "Response allocation failed"));
    }
    return s->error ? http3_progress::failed : http3_progress::input;
}
void http3_request_engine::state::complete(const quic_information_completion& completion) {
    auto it = records.find(completion.stream);
    if (it == records.end() || completion.kind != quic_information_kind::stream || completion.space != quic_pn_space::application) return;
    auto& ids = it->second->information;
    const auto found = std::find(ids.begin(), ids.end(), completion.id);
    if (found != ids.end()) {
        ids.erase(found);
        --(it->second->local ? retained_critical : retained);
        if (it->second->local_role == http3_role::control) connection.acknowledge_control(completion.through_offset);
        retire_drain_request(*it->second);
        check_drain(std::chrono::steady_clock::now());
    }
}
void http3_request_engine::information_completed(const quic_information_completion& completion) {
    auto s = state_;
    state::pump_guard guard(*s);
    s->complete(completion);
}
}  // namespace httpserver::detail
