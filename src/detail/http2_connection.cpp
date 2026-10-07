/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <type_traits>
#include <utility>
#include <httpserver/detail/http2_connection.hpp>
namespace httpserver::detail {
namespace {
http2_error protocol(http2_error_code code = http2_error_code::protocol_error) { return {http2_error_scope::connection, code}; }
http2_error overload() {
    auto e = protocol(http2_error_code::enhance_your_calm);
    e.outcome = http::outcome_code::limit_exceeded;
    e.diagnostic = "HTTP/2 connection capacity exhausted";
    return e;
}
bool valid_rates(const http2_limits& l) {
    return l.control_events_per_interval && l.stream_openings_per_interval &&
        l.control_interval > l.control_interval.zero() && l.stream_interval > l.stream_interval.zero();
}
bool valid_limits(http2_limits l) {
    return l.control_frames > 0 && l.control_frames <= 64 && l.control_bytes >= 62 && l.control_bytes <= 4096 &&
        l.pending_settings > 0 && l.pending_settings <= 16 && l.frames_per_turn > 0 && l.frames_per_turn <= 64 && l.settings_timeout > l.settings_timeout.zero() && valid_rates(l);
}
bool valid_settings(const http2_settings& s) {
    return s.enable_push == 1 && s.initial_window_size <= 0x7fffffff && s.max_frame_size >= 16384 && s.max_frame_size <= 0xffffff;
}
void put32(std::uint8_t* p, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(n >> ((3 - i) * 8));
}
void tuple(std::uint8_t* p, unsigned id, std::uint32_t n) {
    p[0] = 0;
    p[1] = static_cast<std::uint8_t>(id);
    put32(p + 2, n);
}
}  // namespace
bool http2_connection::rate_window::take(time_point now, std::size_t allowance, std::chrono::steady_clock::duration interval) {
    // Unsigned subtraction is exact across the signed clock epoch boundary,
    // avoiding overflow even at time_point::min()/max(). Backward time cannot
    // refill a window or move its anchor.
    using ticks = std::make_unsigned_t<std::chrono::steady_clock::duration::rep>;
    if (!start || (now >= *start && static_cast<ticks>(now.time_since_epoch().count()) -
        static_cast<ticks>(start->time_since_epoch().count()) >= static_cast<ticks>(interval.count()))) {
        start = now; used = 0;
    }
    if (used >= allowance) return false;
    ++used; return true;
}
std::optional<http2_error> http2_connection::open_stream(time_point now) {
    if (!failure_ && !stream_rate_.take(now, limits_.stream_openings_per_interval, limits_.stream_interval)) fail(overload());
    return failure_;
}
http2_connection::http2_connection(server::resource_budget budget, http2_limits limits, http2_settings initial, bool reserve_frames)
    : parser_(budget), compression_(budget), limits_(limits) {
    if (!valid_limits(limits_) || !valid_settings(initial)) {
        auto e = protocol();
        e.outcome = http::outcome_code::invalid_argument;
        fail(e);
        return;
    }
    if (!budget.reserve(server::resource::response_queue_bytes, limits_.control_bytes, control_charge_).ok()) {
        fail(overload());
        return;
    }
    if (reserve_frames && !parser_.reserve_frame_storage(initial.max_frame_size)) {
        fail(overload()); return;
    }
    queue_settings(initial);
}
bool http2_connection::enqueue(slot value) {
    if (count_ >= limits_.control_frames || value.size > limits_.control_bytes - 17 - bytes_) return false;
    bytes_ += value.size;
    slots_[(head_ + count_) % slots_.size()] = std::move(value);
    ++count_;
    return true;
}
http::outcome_code http2_connection::queue_settings(http2_settings settings) {
    if (failure_) return failure_->outcome;
    if (!settings.max_concurrent_streams) settings.max_concurrent_streams = advertised_.max_concurrent_streams;
    if (!settings.max_header_list_size) settings.max_header_list_size = advertised_.max_header_list_size;
    if (!valid_settings(settings)) return http::outcome_code::invalid_argument;
    if (pending_count_ + queued_settings_ >= limits_.pending_settings) return http::outcome_code::limit_exceeded;
    slot value;
    value.bytes[3] = 4;
    value.size = 9;
    const auto add = [&](unsigned id, std::uint32_t n) {
        tuple(value.bytes.data() + value.size, id, n);
        value.size += 6;
    };
    add(1, settings.header_table_size);
    if (settings.max_concurrent_streams) add(3, *settings.max_concurrent_streams);
    add(4, settings.initial_window_size);
    add(5, settings.max_frame_size);
    if (settings.max_header_list_size) add(6, *settings.max_header_list_size);
    value.bytes[2] = static_cast<std::uint8_t>(value.size - 9);
    value.settings = settings;
    if (!enqueue(std::move(value))) return http::outcome_code::limit_exceeded;
    advertised_ = settings;
    ++queued_settings_;
    return http::outcome_code::ok;
}
http::outcome_code http2_connection::queue_window_update(std::uint32_t stream, std::uint32_t increment) {
    if (failure_) return failure_->outcome;
    if (!increment || increment > 0x7fffffff || stream > 0x7fffffff) return http::outcome_code::invalid_argument;
    slot value; value.size = 13; value.bytes[2] = 4; value.bytes[3] = 8;
    put32(value.bytes.data() + 5, stream); put32(value.bytes.data() + 9, increment);
    return enqueue(std::move(value)) ? http::outcome_code::ok : http::outcome_code::limit_exceeded;
}
void http2_connection::discard_stream_credit(std::uint32_t stream) {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        const auto source = (head_ + i) % slots_.size();
        auto& value = slots_[source];
        const auto id = (std::uint32_t{value.bytes[5]} << 24) | (value.bytes[6] << 16) | (value.bytes[7] << 8) | value.bytes[8];
        if (!value.exposed && value.bytes[3] == 8 && id == stream) {
            bytes_ -= value.size; value = {}; continue;
        }
        const auto destination = (head_ + kept++) % slots_.size();
        if (destination != source) {
            slots_[destination] = std::move(value); value = {};
        }
    }
    count_ = kept;
}
void http2_connection::commit(slot& value, time_point now) {
    if (!value.settings) return;
    const auto last_safe_start = time_point::max() - limits_.settings_timeout;
    const auto deadline = now >= last_safe_start ? time_point::max() : now + limits_.settings_timeout;
    pending_[(pending_head_ + pending_count_) % pending_.size()] = {*value.settings, deadline};
    ++pending_count_;
    --queued_settings_;
    local_ = *value.settings;
    parser_.maximum_frame_size(local_.max_frame_size);
    value.settings.reset();
}
std::span<const std::uint8_t> http2_connection::output(time_point now) {
    if (count_) {
        auto& value = slots_[head_];
        commit(value, now);
        value.exposed = true;
        return std::span(value.bytes).subspan(value.used, value.size - value.used);
    }
    return std::span(terminal_).subspan(terminal_used_);
}
bool http2_connection::advance_output(std::size_t count) {
    if (!count_) {
        if (count > terminal_.size() - terminal_used_) return false;
        terminal_used_ += count;
        if (failure_ && terminal_used_ == terminal_.size()) control_charge_.release();
        return true;
    }
    auto& value = slots_[head_];
    if (value.settings || count > value.size - value.used) return false;
    value.used += count;
    if (value.used == value.size) {
        bytes_ -= value.size;
        value = {};
        head_ = (head_ + 1) % slots_.size();
        --count_;
    }
    return true;
}
void http2_connection::fail(http2_error e) {
    if (failure_ || e.scope == http2_error_scope::stream) return;
    failure_ = e;
    parser_.clear();
    // A started wire frame must finish before the terminal frame. Preserve its
    // storage as well, since the owner may still hold the borrowed output span.
    const bool started = count_ && slots_[head_].exposed;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (!started || i != head_) slots_[i] = {};
    }
    count_ = started ? 1 : 0;
    bytes_ = started ? slots_[head_].size : 0;
    queued_settings_ = pending_count_ = 0;
    terminal_.fill(0);
    terminal_[2] = 8;
    terminal_[3] = 7;
    put32(terminal_.data() + 9, last_processed_stream_);
    put32(terminal_.data() + 13, static_cast<std::uint32_t>(e.wire_code));
    terminal_used_ = control_charge_.owns() ? 0 : terminal_.size();
}
std::optional<http2_error> http2_connection::control() {
    const auto h = parser_.header();
    slot response;
    response.size = 9;
    response.bytes[3] = h.type;
    response.bytes[4] = 1;
    if (h.type == 4 && (h.flags & 1)) {
        if (!pending_count_) return protocol();  // Unsolicited ACK is a connection error.
        const auto snapshot = pending_[pending_head_].settings;
        acknowledged_window_ = snapshot.initial_window_size;
        compression_.decoder().acknowledge_maximum(snapshot.header_table_size);
        pending_head_ = (pending_head_ + 1) % pending_.size();
        --pending_count_;
        return {};
    }
    if (h.type == 4) {
        // Admit the ACK before changing externally visible peer state.
        if (!enqueue(response)) return overload();
        peer_ = parser_.settings();
        if (auto minimum = parser_.table_minimum()) compression_.encoder().set_peer_maximum(*minimum);
        compression_.encoder().set_peer_maximum(peer_.header_table_size);
        parser_.peer_settings(peer_);
        return {};
    }
    if (h.flags & 1) return {};
    response.size = 17;
    response.bytes[2] = 8;
    std::copy(parser_.control_payload().begin(), parser_.control_payload().end(), response.bytes.begin() + 9);
    if (!enqueue(response)) return overload();
    return {};
}
std::optional<http2_error> http2_connection::check_timeout(time_point now) {
    if (failure_) return failure_;
    if (pending_count_ && now >= pending_[pending_head_].deadline) fail(protocol(http2_error_code::settings_timeout));
    return failure_;
}
bool http2_connection::charge_frame(const http2_feed_result& result, time_point now) {
    const bool recoverable = result.error && result.error->scope == http2_error_scope::stream;
    if (!recoverable && (result.progress != http2_progress::frame_ready || !result.consumed)) return true;
    ++processed_;
    const auto type = parser_.header().type;
    if (type == 0) return true;
    if (control_rate_.take(now, limits_.control_events_per_interval, limits_.control_interval)) return true;
    fail(overload()); return false;
}
http2_feed_result http2_connection::feed(std::span<const std::uint8_t> bytes, time_point now) {
    if (auto e = check_timeout(now)) return {http2_progress::failed, 0, e};
    if (processed_ >= limits_.frames_per_turn) return {http2_progress::yield, 0, {}};
    auto result = parser_.feed(bytes);
    if (!charge_frame(result, now)) return {http2_progress::failed, result.consumed, failure_};
    if (result.error) {
        fail(*result.error); return result;
    }
    if (result.progress != http2_progress::frame_ready || result.consumed == 0) return result;
    if (parser_.header().type != 4 && parser_.header().type != 6) return result;
    if (auto e = control()) {
        fail(*e);
        return {http2_progress::failed, result.consumed, e};
    }
    parser_.release_frame();
    return {http2_progress::control_ready, result.consumed, {}};
}
http2_feed_result http2_connection::eof() {
    if (failure_) return {http2_progress::failed, 0, failure_};
    auto result = parser_.eof();
    if (result.error) fail(*result.error);
    return result;
}
}  // namespace httpserver::detail
