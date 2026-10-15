/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/http1_body_decoder.hpp>

namespace httpserver::detail {

auto http1_body_budget::from_budget_limits(const server::budget_limits& limits) noexcept -> http1_body_budget {
    http1_body_budget budget;
    budget.max_staged_bytes = limits.get(server::resource::body_buffer_bytes);
    budget.max_trailer_fields = limits.get(server::resource::header_fields);
    return budget;
}

http1_body_decoder::http1_body_decoder(const http1_body_mode& mode, http1_body_budget budget, server::resource_budget scope)
    : budget_(budget),
      storage_charge_(scope, server::resource::body_buffer_bytes),
      trailer_bytes_(scope, server::resource::header_bytes),
      trailer_fields_(scope, server::resource::header_fields),
      kind_(mode.kind),
      failure_(mode.failure) {
    switch (kind_) {
        case http1_body_kind::none:
            phase_ = phase::message_end;
            break;
        case http1_body_kind::length:
            length_remaining_ = mode.content_length;
            phase_ = mode.content_length == 0 ? phase::message_end : phase::length;
            break;
        case http1_body_kind::chunked:
            phase_ = phase::chunk_size;
            break;
        case http1_body_kind::rejected:
            // failure_ already carries the mode's rejection.
            close_policy_ = mode.close_policy;
            failed_ = true;
            break;
    }
}

auto http1_body_decoder::decode(std::string_view wire) -> http1_body_progress {
    if (failed_) return {http1_body_decode::failed, 0};
    if (phase_ == phase::message_end) {
        return {http1_body_decode::complete, 0};
    }
    const absorbed total = absorb_loop(wire);
    if (failed_) return {http1_body_decode::failed, total.consumed};
    if (total.complete) {
        return {http1_body_decode::complete, total.consumed};
    }
    // A full staging queue blocks with zero further progress —
    // progressed (pull to release room), never need_more (more
    // wire will not help). Otherwise the wire simply ran out
    // mid-framing: feed more.
    if (total.blocked) return {http1_body_decode::progressed, total.consumed};
    return {http1_body_decode::need_more, total.consumed};
}

auto http1_body_decoder::pull(std::span<std::byte> into) -> detail::body_pull_result {
    if (failed_) return {detail::body_pull::failed, 0};
    if (!into.empty() && !staging_.empty()) {
        std::vector<std::byte>& front = staging_.front();
        const std::size_t n = std::min(into.size(), front.size());
        std::copy_n(front.begin(), n, into.begin());
        staged_total_ -= n;
        if (n == front.size()) {
            storage_charge_.shrink(front.capacity() + 128);
            staging_.pop_front();
        } else {
            front.erase(front.begin(), front.begin() + static_cast<std::ptrdiff_t>(n));
        }
        return {detail::body_pull::data, n};
    }
    if (staging_.empty() && phase_ == phase::message_end) {
        return {detail::body_pull::end, 0};
    }
    return {detail::body_pull::empty, 0};
}

auto http1_body_decoder::absorb_loop(std::string_view wire) -> absorbed {
    absorbed total;
    while (!wire.empty()) {
        const absorbed step = absorb_phase(wire);
        total.consumed += step.consumed;
        total.complete = step.complete;
        total.blocked = step.blocked;
        wire.remove_prefix(step.consumed);
        if (failed_ || step.complete || step.consumed == 0) break;
    }
    return total;
}

auto http1_body_decoder::absorb_phase(std::string_view wire) -> absorbed {
    switch (phase_) {
        case phase::length:
            return decode_length(wire);
        case phase::chunk_size:
            return decode_chunk_size(wire);
        case phase::chunk_data:
            return decode_chunk_data(wire);
        case phase::chunk_data_end:
            return decode_chunk_data_end(wire);
        case phase::trailer_line:
            return decode_trailer_line(wire);
        case phase::message_end:
            break;
    }
    return {};
}

auto http1_body_decoder::decode_length(std::string_view wire) -> absorbed {
    const std::size_t room = budget_.max_staged_bytes - staged_total_;
    if (room == 0) return {0, false, true};
    const std::size_t n = std::min({wire.size(), room, static_cast<std::size_t>(length_remaining_)});
    if (n > 0) {
        if (!stage(wire.substr(0, n))) return {0, false};
        length_remaining_ -= n;
    }
    if (length_remaining_ == 0) {
        phase_ = phase::message_end;
        return {n, true};
    }
    return {n, false};
}

auto http1_body_decoder::decode_chunk_size(std::string_view wire) -> absorbed {
    std::size_t consumed = 0;
    while (!wire.empty()) {
        const char c = wire.front();
        wire.remove_prefix(1);
        ++consumed;
        if (saw_cr_) {
            saw_cr_ = false;
            if (c != '\n') {
                return line_failed("bare CR in chunk-size line", consumed);
            }
            return complete_chunk_size(consumed);
        }
        if (c == '\r') {
            saw_cr_ = true;
            continue;
        }
        if (c == '\n') {
            return line_failed("bare LF in chunk-size line", consumed);
        }
        if (line_.size() >= kMaxChunkSizeLineBytes) {
            return line_failed("chunk-size line too long", consumed);
        }
        line_.push_back(c);
    }
    return {consumed, false};
}

auto http1_body_decoder::complete_chunk_size(std::size_t consumed) -> absorbed {
    static constexpr std::string_view kHexDigits = "0123456789abcdefABCDEF";
    // Own the bytes: line_ is reset below, and a view into it
    // would be clobbered by the clear()'s null terminator.
    const std::string line = std::move(line_);
    reset_line();
    const std::size_t ext_start = line.find_first_not_of(kHexDigits);
    const std::string_view hex = ext_start == std::string_view::npos ? std::string_view(line) : std::string_view(line).substr(0, ext_start);
    if (hex.empty()) {
        return line_failed("chunk-size is not 1*HEXDIG", consumed);
    }
    // 16 hex digits saturate std::uint64_t exactly; more can never
    // be framed.
    if (hex.size() > 16) {
        return line_failed("chunk-size hex overflow", consumed);
    }
    std::uint64_t size = 0;
    for (const char c : hex) {
        size = (size << 4) + hex_value(c);
    }
    if (!parse_chunk_ext(ext_start == std::string_view::npos ? std::string_view() : std::string_view(line).substr(ext_start))) {
        return line_failed("malformed chunk extension", consumed);
    }
    if (size == 0) {
        phase_ = phase::trailer_line;
        return {consumed, false};
    }
    length_remaining_ = size;
    phase_ = phase::chunk_data;
    return {consumed, false};
}

auto http1_body_decoder::decode_chunk_data(std::string_view wire) -> absorbed {
    const std::size_t room = budget_.max_staged_bytes - staged_total_;
    if (room == 0) return {0, false, true};
    const std::size_t n = std::min({wire.size(), room, static_cast<std::size_t>(length_remaining_)});
    if (n > 0) {
        if (!stage(wire.substr(0, n))) return {0, false};
        length_remaining_ -= n;
    }
    if (length_remaining_ == 0) phase_ = phase::chunk_data_end;
    return {n, false};
}

auto http1_body_decoder::decode_chunk_data_end(std::string_view wire) -> absorbed {
    std::size_t consumed = 0;
    while (!wire.empty()) {
        const char expected = end_seen_ == 0 ? '\r' : '\n';
        if (wire.front() != expected) {
            return line_failed("chunk data is not terminated by CRLF", consumed);
        }
        wire.remove_prefix(1);
        ++consumed;
        if (++end_seen_ == 2) {
            end_seen_ = 0;
            phase_ = phase::chunk_size;
            return {consumed, false};
        }
    }
    return {consumed, false};
}

auto http1_body_decoder::decode_trailer_line(std::string_view wire) -> absorbed {
    std::size_t consumed = 0;
    while (!wire.empty()) {
        const char c = wire.front();
        wire.remove_prefix(1);
        ++consumed;
        if (saw_cr_) {
            saw_cr_ = false;
            if (c != '\n') {
                return line_failed("bare CR in trailer line", consumed);
            }
            return complete_trailer_line(consumed);
        }
        if (c == '\r') {
            saw_cr_ = true;
            continue;
        }
        if (c == '\n') {
            return line_failed("bare LF in trailer line", consumed);
        }
        if (line_.size() >= budget_.max_trailer_bytes) {
            fail(http::outcome_code::limit_exceeded, "trailer bytes budget exhausted");
            return {consumed, false};
        }
        if (!trailer_bytes_.grow(3)) {
            fail(http::outcome_code::limit_exceeded, "shared trailer bytes exhausted");
            return {consumed, false};
        }
        line_.push_back(c);
    }
    return {consumed, false};
}

auto http1_body_decoder::complete_trailer_line(std::size_t consumed) -> absorbed {
    if (line_.empty()) {  // the empty line ends the trailer section
        reset_line();
        phase_ = phase::message_end;
        return {consumed, true};
    }
    // Own the bytes: the accumulated line must survive line_'s
    // reset (a view would be clobbered by the clear()'s null
    // terminator).
    const std::string line = std::move(line_);
    reset_line();
    accept_trailer(line);  // a rejection here is sticky
    return {consumed, false};
}

auto http1_body_decoder::accept_trailer(std::string_view line) -> bool {
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return fail(http::outcome_code::protocol_error, "trailer line has no colon");
    }
    const std::string_view name = line.substr(0, colon);
    if (!http::detail::is_token(name)) {
        return fail(http::outcome_code::protocol_error, "trailer field name is not a token");
    }
    const std::string_view value = detail_head::trim_ows(line.substr(colon + 1));
    if (detail_head::contains_field_value_ctl(value)) {
        return fail(http::outcome_code::protocol_error, "CTL byte in trailer value");
    }
    if (trailers_.size() >= budget_.max_trailer_fields) {
        return fail(http::outcome_code::limit_exceeded, "trailer fields budget exhausted");
    }
    if (is_forbidden_trailer(name)) {
        return fail(http::outcome_code::protocol_error, "forbidden trailer field");
    }
    if (!trailer_fields_.grow(1)) return fail(http::outcome_code::limit_exceeded, "shared trailer fields exhausted");
    trailers_.append(name, value);
    return true;
}

auto http1_body_decoder::is_forbidden_trailer(std::string_view name) const noexcept -> bool {
    static constexpr std::string_view kForbidden[] = {
        "content-length", "transfer-encoding", "host", "te", "connection", "expect", "upgrade", "authorization",
    };
    for (const std::string_view forbidden : kForbidden) {
        if (detail_head::ascii_iequals(name, forbidden)) return true;
    }
    return false;
}

auto http1_body_decoder::parse_chunk_ext(std::string_view ext) -> bool {
    for (;;) {
        ext = skip_bws(ext);
        if (ext.empty()) return true;
        if (ext.front() != ';') return false;
        ext = skip_bws(ext.substr(1));
        const std::size_t name = token_span(ext);
        if (name == 0) return false;
        ext = skip_bws(ext.substr(name));
        if (ext.empty()) return true;
        if (ext.front() != '=') return false;
        ext = skip_bws(ext.substr(1));
        const std::size_t val = chunk_ext_val_span(ext);
        if (val == std::string_view::npos) return false;
        ext = ext.substr(val);
    }
}

auto http1_body_decoder::chunk_ext_val_span(std::string_view val) -> std::size_t {
    if (val.empty()) return std::string_view::npos;
    if (val.front() != '"') return val_span(val);
    const std::size_t end = quoted_string_span(val.substr(1));
    return end == std::string_view::npos ? std::string_view::npos : end + 1;
}

auto http1_body_decoder::val_span(std::string_view v) noexcept -> std::size_t {
    std::size_t n = 0;
    while (n < v.size() && (is_tchar(v[n]) || v[n] == '=')) ++n;
    return n;
}

auto http1_body_decoder::quoted_string_span(std::string_view body) -> std::size_t {
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') return i + 1;
        if (c == '\\') {
            if (i + 1 >= body.size() || !is_quoted_pair_target(body[i + 1])) {
                return std::string_view::npos;
            }
            ++i;
            continue;
        }
        if (!is_qdtext(c)) return std::string_view::npos;
    }
    return std::string_view::npos;  // unterminated
}

auto http1_body_decoder::is_tchar(const char c) noexcept -> bool {
    static constexpr std::string_view kTcharExtra = "!#$%&'*+-.^_`|~";
    return is_ascii_alnum(c) || kTcharExtra.find(c) != std::string_view::npos;
}

auto http1_body_decoder::skip_bws(std::string_view v) noexcept -> std::string_view {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
        v.remove_prefix(1);
    }
    return v;
}

auto http1_body_decoder::token_span(std::string_view v) noexcept -> std::size_t {
    std::size_t n = 0;
    while (n < v.size() && is_tchar(v[n])) ++n;
    return n;
}

auto http1_body_decoder::hex_value(const char c) noexcept -> unsigned char {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned char>(c - 'a' + 10);
    }
    return static_cast<unsigned char>(c - 'A' + 10);
}

auto http1_body_decoder::stage(std::string_view chunk) -> bool {
    if (!storage_charge_.grow(chunk.size() + 128)) return fail(http::outcome_code::limit_exceeded, "shared body storage exhausted");
    std::vector<std::byte> segment;
    segment.reserve(chunk.size());
    const auto* first = reinterpret_cast<const std::byte*>(chunk.data());
    segment.assign(first, first + chunk.size());
    staged_total_ += chunk.size();
    staging_.push_back(std::move(segment));
    return true;
}

auto http1_body_decoder::fail(http::outcome_code code, std::string message) -> bool {
    failure_ = http::outcome(code, "http1_body_decoder: " + std::move(message));
    close_policy_ = code == http::outcome_code::limit_exceeded ? http1_close_policy::respond_then_close : http1_close_policy::close_now;
    failed_ = true;
    return false;
}

auto http1_body_decoder::line_failed(const char* what, std::size_t consumed) -> absorbed {
    fail(http::outcome_code::protocol_error, what);
    return {consumed, false};
}

auto http1_body_decoder::reset_line() -> void {
    line_.clear();
    saw_cr_ = false;
}

}  // namespace httpserver::detail
