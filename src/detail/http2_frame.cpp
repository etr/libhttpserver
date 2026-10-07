/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <new>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_frame.hpp>
namespace httpserver::detail {
namespace {
std::uint32_t u32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}
http2_error error(http2_error_code code, std::uint32_t stream = 0) {
    return {stream ? http2_error_scope::stream : http2_error_scope::connection, code, stream};
}
std::optional<http2_error> placement(http2_frame_header h) {
    // 1 requires a stream, -1 requires connection zero, 0 has no placement rule.
    constexpr std::array<int, 10> rules{1, 1, 1, 1, -1, 0, -1, -1, 0, 1};
    if (h.type == 5) return error(http2_error_code::protocol_error);
    if (h.type >= rules.size()) return {};
    if (rules[h.type] == 1 && !h.stream_id) return error(http2_error_code::protocol_error);
    if (rules[h.type] == -1 && h.stream_id) return error(http2_error_code::protocol_error);
    return {};
}
std::optional<http2_error> settings_shape(http2_frame_header h) {
    if ((h.length % 6) || ((h.flags & 1) && h.length)) return error(http2_error_code::frame_size_error);
    return {};
}
std::optional<http2_error> shape(http2_frame_header h) {
    const auto size = http2_error_code::frame_size_error;
    constexpr std::array<unsigned, 10> exact{0, 0, 5, 4, 0, 0, 8, 0, 4, 0};
    if (h.type >= exact.size()) return {};
    if (exact[h.type] && h.length != exact[h.type]) return error(size, h.type == 2 ? h.stream_id : 0);
    if (h.type == 4) return settings_shape(h);
    if (h.type == 7 && h.length < 8) return error(size);
    return {};
}
std::optional<http2_error> prefix_shape(http2_frame_header h) {
    if (h.type > 1) return {};
    const unsigned minimum = ((h.flags & 8) ? 1 : 0) + ((h.type == 1 && (h.flags & 32)) ? 5 : 0);
    if (h.length < minimum) return error(http2_error_code::frame_size_error);
    return {};
}
std::optional<http2_error> setting_range(unsigned id, std::uint32_t value) {
    if (id == 2 && value > 1) return error(http2_error_code::protocol_error);
    if (id == 4 && value > 0x7fffffff) return error(http2_error_code::flow_control_error);
    if (id == 5 && (value < 16384 || value > 0xffffff)) return error(http2_error_code::protocol_error);
    return {};
}
std::optional<http2_error> apply_setting(http2_settings& s, unsigned id, std::uint32_t value) {
    if (auto e = setting_range(id, value)) return e;
    switch (id) {
        case 1: s.header_table_size = value; break;
        case 2:
            s.enable_push = value;
            break;
        case 3: s.max_concurrent_streams = value; break;
        case 4:
            s.initial_window_size = value;
            break;
        case 5:
            s.max_frame_size = value;
            break;
        case 6: s.max_header_list_size = value; break;
        default: break;
    }
    return {};
}
}  // namespace
http2_feed_result http2_frame_parser::fail(http2_error failure, std::size_t consumed) {
    if (failure.scope == http2_error_scope::connection) {
        error_ = failure;
        clear();
    }
    return {http2_progress::failed, consumed, failure};
}
void http2_frame_parser::clear() {
    std::vector<std::uint8_t>().swap(payload_);
    retained_.release();
}
void http2_frame_parser::release_frame() {
    clear();
    ready_ = started_ = false;
    header_used_ = payload_used_ = 0;
    deferred_.reset();
}
std::optional<http2_error> http2_frame_parser::start_frame() {
    frame_ = {(std::uint32_t{header_bytes_[0]} << 16) | (std::uint32_t{header_bytes_[1]} << 8) | header_bytes_[2],
        header_bytes_[3], header_bytes_[4], u32(header_bytes_.data() + 5) & 0x7fffffff};
    if (auto e = header_rules()) return e;
    settings_ = peer_;
    table_minimum_.reset();
    scratch_.fill(0);
    return retain_payload();
}
std::optional<http2_error> http2_frame_parser::header_rules() {
    if (frame_.length > maximum_) return error(http2_error_code::frame_size_error);
    if (initial_ && (frame_.type != 4 || (frame_.flags & 1))) return error(http2_error_code::protocol_error);
    if (auto e = sequence_rules()) return e;
    if (auto e = placement(frame_)) return e;
    if (auto e = shape(frame_)) return e;
    return prefix_shape(frame_);
}
std::optional<http2_error> http2_frame_parser::sequence_rules() {
    if (continuation_ && (frame_.type != 9 || frame_.stream_id != continuation_)) return error(http2_error_code::protocol_error);
    if (!continuation_ && frame_.type == 9) return error(http2_error_code::protocol_error);
    return {};
}
std::optional<http2_error> http2_frame_parser::retain_payload() {
    const bool retain = frame_.type <= 9 && frame_.type != 4 && frame_.type != 6;
    if (!retain || !frame_.length) return {};
    if (!budget_.reserve(server::resource::body_buffer_bytes, frame_.length, retained_).ok()) {
        auto e = error(http2_error_code::enhance_your_calm);
        e.outcome = http::outcome_code::limit_exceeded;
        return e;
    }
    try {
        payload_.resize(frame_.length);
    } catch (const std::bad_alloc&) {
        auto e = error(http2_error_code::enhance_your_calm);
        e.outcome = http::outcome_code::limit_exceeded;
        return e;
    }
    return {};
}
void http2_frame_parser::take_control(std::uint8_t byte) {
    if (frame_.type == 6) scratch_[payload_used_] = byte;
    if (frame_.type != 4) return;
    const auto index = payload_used_ % 6;
    scratch_[index] = byte;
    if (index != 5 || deferred_) return;
    const auto id = (unsigned{scratch_[0]} << 8) | scratch_[1];
    const auto value = u32(scratch_.data() + 2);
    deferred_ = apply_setting(settings_, id, value);
    if (id == 1) table_minimum_ = std::min(table_minimum_.value_or(value), value);
}
std::optional<http2_error> http2_frame_parser::payload_rules() {
    if ((frame_.type == 0 || frame_.type == 1) && (frame_.flags & 8)) {
        const std::size_t prefix = frame_.type == 1 && (frame_.flags & 32) ? 6 : 1;
        if (payload_[0] > frame_.length - prefix) return error(http2_error_code::protocol_error);
    }
    if (frame_.type == 8 && !(u32(payload_.data()) & 0x7fffffff)) return error(http2_error_code::protocol_error, frame_.stream_id);
    return {};
}
std::optional<http2_error> http2_frame_parser::priority_rules() {
    std::optional<std::size_t> offset;
    if (frame_.type == 2) offset = 0;
    if (frame_.type == 1 && (frame_.flags & 32)) offset = (frame_.flags & 8) ? 1 : 0;
    if (offset && (u32(payload_.data() + *offset) & 0x7fffffff) == frame_.stream_id) return error(http2_error_code::protocol_error, frame_.stream_id);
    return {};
}
std::optional<http2_error> http2_frame_parser::finish_frame() {
    if (deferred_) return deferred_;
    if (auto e = payload_rules()) return e;
    if (frame_.type == 1 && !(frame_.flags & 4)) continuation_ = frame_.stream_id;
    if (frame_.type == 9 && (frame_.flags & 4)) continuation_ = 0;
    initial_ = false;
    return priority_rules();
}
std::optional<http2_error> http2_frame_parser::take_prefix(std::span<const std::uint8_t> bytes, std::size_t& used) {
    while (magic_used_ < http2_magic.size() && used < bytes.size()) {
        if (bytes[used++] != static_cast<std::uint8_t>(http2_magic[magic_used_++])) return error(http2_error_code::protocol_error);
    }
    if (magic_used_ != http2_magic.size()) return {};
    while (header_used_ < 9 && used < bytes.size()) header_bytes_[header_used_++] = bytes[used++];
    return {};
}
void http2_frame_parser::take_payload(std::span<const std::uint8_t> bytes, std::size_t& used) {
    const auto count = std::min<std::size_t>(bytes.size() - used, frame_.length - payload_used_);
    for (std::size_t i = 0; i < count; ++i) {
        const auto byte = bytes[used++];
        if (!payload_.empty()) payload_[payload_used_] = byte;
        if (!deferred_) take_control(byte);
        ++payload_used_;
    }
}
http2_feed_result http2_frame_parser::feed(std::span<const std::uint8_t> bytes) {
    if (error_) return {http2_progress::failed, 0, error_};
    if (ready_) return {http2_progress::frame_ready, 0, {}};
    std::size_t used = 0;
    if (auto e = take_prefix(bytes, used)) return fail(*e, used);
    if (header_used_ < 9) return {http2_progress::input, used, {}};
    if (!started_) {
        started_ = true;
        if (auto e = start_frame()) {
            if (e->scope == http2_error_scope::connection) return fail(*e, used);
            deferred_ = e;
        }
    }
    take_payload(bytes, used);
    if (payload_used_ < frame_.length) return {http2_progress::input, used, {}};
    ready_ = true;
    if (auto e = finish_frame()) return fail(*e, used);
    return {http2_progress::frame_ready, used, {}};
}
http2_feed_result http2_frame_parser::eof() {
    if (error_) return {http2_progress::failed, 0, error_};
    if (initial_ || continuation_ || (!ready_ && header_used_)) return fail(error(http2_error_code::protocol_error), 0);
    return {};
}
}  // namespace httpserver::detail
