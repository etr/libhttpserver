/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <new>
#include <memory>
#include <initializer_list>
#include <vector>
#include <utility>
#include <httpserver/detail/http3_connection.hpp>
namespace httpserver::detail {
namespace {
bool is_frame(std::uint64_t type, std::initializer_list<std::uint64_t> types) {
    return std::find(types.begin(), types.end(), type) != types.end();
}
bool valid_limits(const http3_limits& limits) {
    const auto maximum = server::max_capacity(server::resource::quic_reassembly_bytes);
    if (limits.headers.max_fields > maximum / sizeof(qpack_field)) return false;
    return limits.headers.max_expanded_bytes <= maximum - limits.headers.max_fields * sizeof(qpack_field);
}
http3_progress progress_for(bool error, bool ready, std::size_t frames_left) {
    if (error) return http3_progress::failed;
    if (ready) return http3_progress::event;
    if (!frames_left) return http3_progress::yield;
    return http3_progress::input;
}
}  // namespace
http3_connection::http3_connection(quic_storage_lease data, quic_storage_lease critical, http3_limits limits)
    : data_(std::move(data)), critical_(std::move(critical)), limits_(limits), frames_left_(limits.frames_per_turn) {
    if (!valid_limits(limits_)) {
        fail(0x107, "Decoded HEADERS storage bounds invalid", 0); return;
    }
    if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, sizeof(http3_peer_settings) + 512, metadata_).ok())
        fail(0x107, "Critical connection storage exhausted", 0);
}
std::optional<http3_error> http3_connection::fail(std::uint64_t code, std::string_view diagnostic, std::uint64_t id) {
    if (!error_) error_ = h3_error(code, diagnostic, id);
    return error_;
}
std::optional<http3_error> http3_connection::attach_stream(std::uint64_t id) {
    if (error_) return error_;
    if (id > k_quic_max_integer || (id & 1) || streams_.contains(id)) return fail(0x103, "Invalid or recreated peer stream", id);
    if (streams_.size() >= limits_.stream_records) return fail(0x107, "Stream record limit", id);
    server::reservation descriptor, charge;
    try {
        if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, sizeof(stream) + 128, descriptor).ok() ||
            !critical_.budget.reserve(server::resource::streams, 1, charge).ok()) return fail(0x107, "Stream metadata budget exhausted", id);
        auto s = std::make_unique<stream>(); s->descriptor = std::move(descriptor); s->stream_charge = std::move(charge);
        if (!(id & 2)) {
            s->role = http3_role::request; s->parser.emplace(data_, critical_, limits_);
        }
        streams_.emplace(id, std::move(s));
    } catch (const std::bad_alloc&) {
        return fail(0x102, "Stream metadata allocation failed", id);
    }
    return {};
}
bool http3_connection::classify(stream& s, std::uint64_t type, std::uint64_t id) {
    if (type == 1) {
        fail(0x103, "Push streams unsupported", id); return false;
    }
    s.role = http3_role::discard;
    if (type != 0 && type != 2 && type != 3) return true;
    const auto slot = type == 0 ? 0 : static_cast<std::size_t>(type - 1);
    if (critical_seen_[slot]) {
        fail(0x103, "Duplicate critical stream", id); return false;
    }
    critical_seen_[slot] = true;
    s.role = static_cast<http3_role>(static_cast<unsigned>(http3_role::control) + slot);
    if (type == 0) s.parser.emplace(data_, critical_, limits_, s.offset);
    return true;
}
bool http3_connection::qpack_input(stream& s, std::byte byte, std::uint64_t id) {
    const auto b = std::to_integer<unsigned>(byte);
    const bool encoder = s.role == http3_role::qpack_encoder;
    if (!s.scratch_size && encoder) {
        if (b == 0x20) return true;  // Setting capacity to zero is legal.
        fail(0x201, "Dynamic encoder instruction unsupported", id); return false;
    }
    if (!s.scratch_size && (b & 0xc0) != 0x40) {
        fail(0x202, "Decoder instruction lacks dynamic reference", id); return false;
    }
    s.scratch[s.scratch_size++] = byte;
    const auto wire = std::span(reinterpret_cast<const std::uint8_t*>(s.scratch.data()), s.scratch_size);
    const auto decoded = qpack_decode_integer(wire, 6, {});
    if (decoded.status.ok()) {
        s.scratch_size = 0; return true;
    }
    if (decoded.status.state != qpack_state::incomplete || s.scratch_size == s.scratch.size()) {
        fail(0x202, "Malformed Stream Cancellation", id); return false;
    }
    return true;
}
std::size_t http3_connection::uni_input(stream& s, std::span<const std::byte> input, std::uint64_t id) {
    std::size_t count = 0;
    while (count < input.size() && !s.parser && !error_) {
        const auto byte = input[count++]; ++s.offset;
        if (s.role == http3_role::discard) continue;
        if (s.role != http3_role::pending) {
            qpack_input(s, byte, id); continue;
        }
        s.scratch[s.scratch_size++] = byte;
        const auto decoded = decode_quic_varint(std::span(s.scratch).first(s.scratch_size));
        if (decoded.code == quic_codec_code::truncated) continue;
        s.scratch_size = 0; classify(s, decoded.value, id);
    }
    return count;
}
bool http3_connection::admit_control(stream& s, std::uint64_t id) {
    const auto type = s.parser->type();
    if (!peer_.received && type != 4) {
        fail(0x10a, "Control stream requires SETTINGS first", id); return false;
    }
    if (is_frame(type, {0, 1, 5}) || (type == 4 && peer_.received)) {
        fail(0x105, "Unexpected control frame", id); return false;
    }
    return true;
}
bool http3_connection::admit_request(stream& s, std::uint64_t id) {
    const auto type = s.parser->type();
    if (is_frame(type, {3, 4, 5, 7, 13}) || (type == 0 && s.request_stage != 1) || (type == 1 && s.request_stage == 2)) {
        fail(0x105, "Unexpected request frame", id); return false;
    }
    if (type == 1) ++s.request_stage;
    return true;
}
bool http3_connection::admit(stream& s, std::uint64_t id) {
    const bool accepted = s.role == http3_role::control ? admit_control(s, id) : admit_request(s, id);
    if (!accepted) return false;
    if (is_frame(s.parser->type(), {2, 6, 8, 9})) {
        fail(0x105, "Reserved HTTP/2 frame", id); return false;
    }
    if (auto error = s.parser->accept_header()) {
        fail(error->wire_code, error->diagnostic, id); return false;
    }
    return true;
}
bool http3_connection::process_control(const http3_event& e, std::uint64_t id) {
    if (e.type == 4) return settings(e.payload, id);
    if (!is_frame(e.type, {3, 7, 13})) return true;
    const auto scalar = decode_quic_varint(e.payload);
    if (scalar.code != quic_codec_code::ok || scalar.consumed != e.payload.size()) {
        fail(0x106, "Control scalar payload layout", id); return false;
    }
    return true;
}
bool http3_connection::decode_fields(stream& s, std::uint64_t id) {
    const auto overhead = limits_.headers.max_fields * sizeof(qpack_field);
    const auto charge = std::max<std::size_t>(1, limits_.headers.max_expanded_bytes + overhead);
    if (!data_.budget.reserve(server::resource::quic_reassembly_bytes, charge, s.fields_charge).ok()) {
        fail(0x107, "Expanded HEADERS storage exhausted", id); return false;
    }
    const auto payload = s.event.payload;
    auto decoded = decoder_.decode_allocated({reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()}, limits_.headers);
    if (!decoded.status.ok()) {
        s.fields_charge.release(); fail(decoded.status.state == qpack_state::limit_exceeded ? 0x107 : 0x200, "Static QPACK field section rejected", id); return false;
    }
    s.fields = std::move(decoded.fields); s.event.fields = s.fields;
    return true;
}
bool http3_connection::process(stream& s, std::uint64_t id) {
    const auto& e = *s.parser->event();
    if (s.role == http3_role::control) return process_control(e, id);
    if (!is_frame(e.type, {0, 1})) return true;
    s.event = e;
    if (e.type == 1 && !decode_fields(s, id)) return false;
    s.ready = true;
    return true;
}
http3_feed_result http3_connection::feed(std::uint64_t id, std::span<const std::byte> input, std::uint64_t offset) {
    if (error_) return {http3_progress::failed, 0, error_};
    const auto it = streams_.find(id);
    if (it == streams_.end()) return {http3_progress::failed, 0, fail(0x103, "Stream was not attached", id)};
    auto& s = *it->second;
    if (s.closed) return {http3_progress::failed, 0, fail(0x103, "Bytes after terminal stream", id)};
    if (s.ready) return {http3_progress::event, 0, {}};
    if (!h3_ordered_extent(offset, input.size(), s.offset))
        return {http3_progress::failed, 0, fail(0x102, "Invalid ordered transport extent", id)};
    std::size_t consumed = 0;
    if (!s.parser) {
        consumed = uni_input(s, input, id); input = input.subspan(consumed);
    }
    consumed += feed_frames(s, input, id);
    return {progress_for(error_.has_value(), s.ready, frames_left_), consumed, error_};
}
std::size_t http3_connection::feed_frames(stream& s, std::span<const std::byte> input, std::uint64_t id) {
    if (!s.parser || error_) return 0;
    std::size_t consumed = 0;
    while (!s.ready && frames_left_) {
        const auto r = s.parser->feed(input, s.offset);
        consumed += r.consumed; s.offset += r.consumed; input = input.subspan(r.consumed);
        if (!handle_frame(s, r, id)) break;
    }
    if (error_) {
        s.parser.reset(); std::vector<qpack_field>().swap(s.fields); s.fields_charge.release();
    }
    return consumed;
}
bool http3_connection::process_safe(stream& s, std::uint64_t id) {
    try {
        return process(s, id);
    } catch (const std::bad_alloc&) {
        fail(0x102, "Field section allocation failed", id);
    } catch (const std::length_error&) {
        fail(0x102, "Field section allocation size invalid", id);
    }
    return false;
}
bool http3_connection::handle_frame(stream& s, const http3_feed_result& result, std::uint64_t id) {
    if (result.error) {
        fail(result.error->wire_code, result.error->diagnostic, id); return false;
    }
    if (result.progress == http3_progress::header) return admit(s, id);
    if (result.progress != http3_progress::event) return false;
    --frames_left_;
    if (!process_safe(s, id)) return false;
    if (!s.ready) s.parser->release_event();
    return !s.ready;
}
const http3_event* http3_connection::event(std::uint64_t id) const {
    const auto it = streams_.find(id);
    return it != streams_.end() && it->second->ready ? &it->second->event : nullptr;
}
void http3_connection::release_event(std::uint64_t id) {
    const auto it = streams_.find(id);
    if (it == streams_.end() || !it->second->ready) return;
    auto& s = *it->second;
    s.ready = false; s.event = {}; std::vector<qpack_field>().swap(s.fields); s.fields_charge.release(); s.parser->release_event();
}
std::uint64_t http3_connection::offset(std::uint64_t id) const {
    const auto it = streams_.find(id); return it == streams_.end() ? 0 : it->second->offset;
}
std::optional<http3_error> http3_connection::terminal(std::uint64_t id, quic_stream_terminal terminal) {
    if (error_) return error_;
    if (std::find(local_ids_.begin(), local_ids_.end(), id) != local_ids_.end()) return fail(0x104, "Local critical stream closed", id);
    const auto it = streams_.find(id);
    if (it == streams_.end()) return fail(0x103, "Terminal on unattached stream", id);
    auto& s = *it->second;
    if (s.closed) return {};
    if (s.role >= http3_role::control && s.role <= http3_role::qpack_decoder) return fail(0x104, "Critical stream closed", id);
    if (terminal.kind == quic_terminal_kind::eof && clean_terminal(s, id)) return error_;
    s.closed = true; s.scratch = {}; s.scratch_size = 0;
    if (terminal.kind == quic_terminal_kind::reset) {
        release_event(id); s.parser.reset();
    }
    return {};
}
std::optional<http3_error> http3_connection::clean_terminal(stream& s, std::uint64_t id) {
    if (!s.parser) return {};
    if (auto error = s.parser->finish()) return fail(error->wire_code, error->diagnostic, id);
    if (!s.request_stage) return fail(0x105, "Request lacks HEADERS", id);
    return {};
}
}  // namespace httpserver::detail
