/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/forms_multipart.hpp>
#include <string>
#include <utility>
#include <vector>

namespace httpserver::detail::multipart_params {

std::string_view trim(std::string_view v) noexcept {
    while (!v.empty() && is_hspace(v.front())) v.remove_prefix(1);
    while (!v.empty() && is_hspace(v.back())) v.remove_suffix(1);
    return v;
}

std::size_t skip_hspace(std::string_view v, std::size_t at) noexcept {
    while (at < v.size() && is_hspace(v[at])) ++at;
    return at;
}

std::size_t skip_separators(std::string_view v, std::size_t at) noexcept {
    while (at < v.size() && (is_hspace(v[at]) || v[at] == ';')) {
        ++at;
    }
    return at;
}

http::outcome read_quoted(std::string_view v, std::size_t& at, std::string& out) {
    ++at;  // the opening quote
    for (;;) {
        if (at >= v.size()) {
            return http::outcome(http::outcome_code::invalid_argument, "multipart_decoder: unterminated quoted string");
        }
        const char c = v[at];
        if (c == '"') {
            ++at;
            return http::outcome::okay();
        }
        if (c == '\\') {
            if (at + 1 >= v.size()) {
                return http::outcome(http::outcome_code::invalid_argument, "multipart_decoder: truncated quoted-pair");
            }
            out.push_back(v[at + 1]);
            at += 2;
            continue;
        }
        out.push_back(c);
        ++at;
    }
}

http::outcome parse_value(std::string_view v, std::size_t& at, std::string& out) {
    if (at < v.size() && v[at] == '"') {
        return read_quoted(v, at, out);
    }
    const std::size_t start = at;
    while (at < v.size() && v[at] != ';') ++at;
    out.assign(trim(v.substr(start, at - start)));
    return http::outcome::okay();
}

http::outcome parse_param(std::string_view v, std::size_t& at, std::pair<std::string, std::string>& out) {
    const std::size_t name_start = at;
    while (at < v.size() && v[at] != '=' && v[at] != ';' && !is_hspace(v[at])) {
        ++at;
    }
    if (at == name_start) {
        return http::outcome(http::outcome_code::invalid_argument, "multipart_decoder: empty parameter name");
    }
    out.first.assign(v.substr(name_start, at - name_start));
    at = skip_hspace(v, at);
    if (at >= v.size() || v[at] != '=') {
        return http::outcome(http::outcome_code::invalid_argument, "multipart_decoder: parameter without '='");
    }
    ++at;
    at = skip_hspace(v, at);
    return parse_value(v, at, out.second);
}

http::outcome parse_type_params(std::string_view value, std::string& type, std::vector<std::pair<std::string, std::string>>& params) {
    value = trim(value);
    const std::size_t semi = value.find(';');
    type.assign(value.substr(0, semi == std::string_view::npos ? value.size() : semi));
    if (type.empty()) {
        return http::outcome(http::outcome_code::invalid_argument, "multipart_decoder: empty media type");
    }
    if (semi == std::string_view::npos) {
        return http::outcome::okay();
    }
    std::size_t at = semi + 1;
    for (;;) {
        at = skip_separators(value, at);
        if (at >= value.size()) return http::outcome::okay();
        std::pair<std::string, std::string> param;
        const http::outcome parsed = parse_param(value, at, param);
        if (!parsed.ok()) return parsed;
        params.push_back(std::move(param));
    }
}

}  // namespace httpserver::detail::multipart_params

namespace httpserver::detail {

bool is_bchar(char c) noexcept {
    constexpr std::string_view k_special = "'()+_,-./:=? ";
    if (k_special.find(c) != std::string_view::npos) return true;
    const bool digit = c >= '0' && c <= '9';
    const bool upper = c >= 'A' && c <= 'Z';
    const bool lower = c >= 'a' && c <= 'z';
    return digit || upper || lower;
}

http::outcome validate_boundary(std::string_view boundary) {
    if (boundary.empty() || boundary.size() > 256) {
        return http::outcome(http::outcome_code::invalid_argument, "multipart boundary length must be 1..256");
    }
    for (const char c : boundary) {
        if (!is_bchar(c)) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "multipart boundary has a character outside RFC 2046 "
                                 "bchars");
        }
    }
    if (boundary.back() == ' ') {
        return http::outcome(http::outcome_code::invalid_argument, "multipart boundary must not end with a space");
    }
    return http::outcome::okay();
}

http::outcome extract_boundary(std::optional<std::string_view> content_type, std::string& out) {
    if (!content_type.has_value()) {
        return http::outcome(http::outcome_code::invalid_argument, "multipart_decoder: request carries no Content-Type");
    }
    std::string type;
    std::vector<std::pair<std::string, std::string>> params;
    const http::outcome parsed = multipart_params::parse_type_params(*content_type, type, params);
    if (!parsed.ok()) return parsed;
    for (const std::pair<std::string, std::string>& param : params) {
        if (auth_text::ascii_iequal(param.first, "boundary")) {
            const http::outcome valid = validate_boundary(param.second);
            if (!valid.ok()) return valid;
            out = param.second;
            return http::outcome::okay();
        }
    }
    return http::outcome(http::outcome_code::invalid_argument, "multipart_decoder: Content-Type carries no boundary");
}

multipart_decoder::multipart_decoder(const std::string& boundary, std::uint64_t max_total_bytes, std::uint64_t max_parts, std::uint64_t max_part_bytes, std::uint64_t max_part_header_bytes,
                                     multipart_events& events) noexcept
    : needle_("\r\n--" + boundary),
      max_follow_padding_(needle_.size() + 4),
      max_total_bytes_(max_total_bytes),
      max_parts_(max_parts),
      max_part_bytes_(max_part_bytes),
      max_part_header_bytes_(max_part_header_bytes),
      events_(events) {
    // A virtual CRLF makes the stream-start delimiter (which needs
    // no preceding CRLF) the same needle as every later one.
    pending_.assign("\r\n", 2);
}

auto multipart_decoder::feed(std::span<const std::byte> wire) -> http::outcome {
    if (failed_) return failure_;
    if (wire.empty()) return http::outcome::okay();
    std::size_t usable = wire.size();
    bool over = false;
    if (bytes_seen_ + static_cast<std::uint64_t>(wire.size()) > max_total_bytes_) {
        usable = static_cast<std::size_t>(max_total_bytes_ - bytes_seen_);
        over = true;
    }
    const char* raw = reinterpret_cast<const char*>(wire.data());
    pending_.append(raw, usable);
    bytes_seen_ += usable;
    const http::outcome pumped = pump();
    if (!pumped.ok()) return pumped;
    if (over) {
        return fail(http::outcome_code::limit_exceeded, "raw body bytes budget exhausted");
    }
    return http::outcome::okay();
}

auto multipart_decoder::finish() -> http::outcome {
    if (failed_) return failure_;
    if (state_ == state::epilogue) return http::outcome::okay();
    if (state_ == state::preamble && bytes_seen_ == 0) {
        return http::outcome::okay();
    }
    return fail(http::outcome_code::invalid_argument, "multipart body ended without a final boundary");
}

auto multipart_decoder::crlf_at(std::size_t k) const noexcept -> crlf_verdict {
    if (k >= pending_.size()) return crlf_verdict::need_more;
    if (pending_[k] != '\r') return crlf_verdict::no;
    if (k + 1 >= pending_.size()) return crlf_verdict::need_more;
    return pending_[k + 1] == '\n' ? crlf_verdict::yes : crlf_verdict::no;
}

auto multipart_decoder::decide_dash(std::size_t j) noexcept -> verdict {
    if (j + 1 >= pending_.size()) return verdict::need_more;
    if (pending_[j + 1] != '-') return verdict::not_delimiter;
    follow_end_ = j + 2;
    return verdict::final_boundary;
}

auto multipart_decoder::decide_follow(std::size_t j) -> verdict {
    if (j >= pending_.size()) return verdict::need_more;
    if (pending_[j] == '-') return decide_dash(j);
    std::size_t k = j;
    while (k < pending_.size() && multipart_params::is_hspace(pending_[k])) {
        ++k;
    }
    if (k - j > max_follow_padding_) {
        fail(http::outcome_code::invalid_argument,
             "transport padding after boundary exceeds the "
             "accepted bound");
        return verdict::padding_reject;
    }
    const crlf_verdict end = crlf_at(k);
    if (end == crlf_verdict::need_more) return verdict::need_more;
    if (end == crlf_verdict::yes) {
        follow_end_ = k + 2;
        return verdict::next_part;
    }
    return verdict::not_delimiter;
}

auto multipart_decoder::pump() -> http::outcome {
    for (;;) {
        http::outcome step = http::outcome::okay();
        switch (state_) {
            case state::epilogue:
                pending_.clear();
                return step;
            case state::preamble:
                step = pump_preamble();
                break;
            case state::part_headers:
                step = pump_headers();
                break;
            case state::part_body:
                step = pump_body();
                break;
        }
        if (!step.ok() || need_more_) return step;
    }
}

auto multipart_decoder::hold_tail(std::size_t keep) -> void {
    if (pending_.size() > keep) {
        pending_.erase(0, pending_.size() - keep);
    }
}

auto multipart_decoder::pump_preamble() -> http::outcome {
    need_more_ = false;
    std::size_t from = 0;
    for (;;) {
        const std::size_t p = pending_.find(needle_, from);
        if (p == std::string::npos) {
            hold_tail(needle_.size() - 1);
            need_more_ = true;
            return http::outcome::okay();
        }
        const verdict v = decide_follow(p + needle_.size());
        if (v == verdict::padding_reject) return failure_;
        if (v == verdict::need_more) {
            pending_.erase(0, p);
            need_more_ = true;
            return http::outcome::okay();
        }
        if (v == verdict::final_boundary) {
            pending_.erase(0, follow_end_);
            state_ = state::epilogue;
            return http::outcome::okay();
        }
        if (v == verdict::next_part) {
            pending_.erase(0, follow_end_);
            begin_headers();
            return http::outcome::okay();
        }
        from = p + 1;
    }
}

auto multipart_decoder::pump_headers() -> http::outcome {
    need_more_ = false;
    for (;;) {
        const std::size_t nl = pending_.find('\n');
        if (nl == std::string::npos) {
            // An unterminated line can never fit the block budget.
            if (block_bytes_ + pending_.size() > max_part_header_bytes_) {
                return fail(http::outcome_code::limit_exceeded, "part header block budget exhausted");
            }
            need_more_ = true;
            return http::outcome::okay();
        }
        block_bytes_ += nl + 1;
        if (block_bytes_ > max_part_header_bytes_) {
            return fail(http::outcome_code::limit_exceeded, "part header block budget exhausted");
        }
        std::string_view line(pending_.data(), nl);
        if (line.empty() || line.back() != '\r') {
            return fail(http::outcome_code::invalid_argument, "part header line without CRLF");
        }
        line.remove_suffix(1);
        if (line.empty()) {
            pending_.erase(0, nl + 1);
            return begin_part();
        }
        // Copy before erasing: the view addresses pending_'s
        // buffer, which erase moves.
        lines_.emplace_back(line);
        pending_.erase(0, nl + 1);
    }
}

auto multipart_decoder::pump_body() -> http::outcome {
    need_more_ = false;
    std::size_t from = 0;
    for (;;) {
        const std::size_t p = pending_.find(needle_, from);
        if (p == std::string::npos) {
            const std::size_t n = pending_.size() > needle_.size() ? pending_.size() - needle_.size() : 0;
            const http::outcome emitted = emit_data(n);
            if (!emitted.ok()) return emitted;
            pending_.erase(0, n);
            need_more_ = true;
            return http::outcome::okay();
        }
        const verdict v = decide_follow(p + needle_.size());
        if (v == verdict::padding_reject) return failure_;
        if (v == verdict::need_more) {
            const http::outcome emitted = emit_data(p);
            if (!emitted.ok()) return emitted;
            pending_.erase(0, p);
            need_more_ = true;
            return http::outcome::okay();
        }
        if (v == verdict::final_boundary) {
            return end_part_at(p, true);
        }
        if (v == verdict::next_part) {
            return end_part_at(p, false);
        }
        from = p + 1;
    }
}

auto multipart_decoder::end_part_at(std::size_t data_len, bool final_boundary) -> http::outcome {
    const http::outcome emitted = emit_data(data_len);
    if (!emitted.ok()) return emitted;
    pending_.erase(0, follow_end_);
    const http::outcome ended = events_.on_part_end();
    if (!ended.ok()) return fail_with(ended);
    ++parts_completed_;
    if (final_boundary) {
        state_ = state::epilogue;
    } else {
        begin_headers();
    }
    return http::outcome::okay();
}

auto multipart_decoder::emit_data(std::size_t n) -> http::outcome {
    if (n == 0) return http::outcome::okay();
    if (part_bytes_ + n > max_part_bytes_) {
        return fail(http::outcome_code::limit_exceeded, "part body bytes budget exhausted");
    }
    const char* raw = pending_.data();
    const http::outcome emitted = events_.on_part_data(std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw), n));
    if (!emitted.ok()) return fail_with(emitted);
    part_bytes_ += n;
    return http::outcome::okay();
}

auto multipart_decoder::begin_headers() noexcept -> void {
    state_ = state::part_headers;
    lines_.clear();
    block_bytes_ = 0;
    name_.clear();
    filename_.clear();
    content_type_.clear();
    transfer_encoding_.clear();
    saw_disposition_ = false;
    has_name_ = false;
    has_filename_ = false;
}

auto multipart_decoder::begin_part() -> http::outcome {
    const http::outcome ready = finalize_headers();
    if (!ready.ok()) return ready;
    if (parts_begun_ >= max_parts_) {
        return fail(http::outcome_code::limit_exceeded, "parts budget exhausted");
    }
    const http::outcome emitted = events_.on_part_begin(multipart_part_meta{name_, filename_, content_type_, transfer_encoding_});
    if (!emitted.ok()) return fail_with(emitted);
    ++parts_begun_;
    state_ = state::part_body;
    part_bytes_ = 0;
    return http::outcome::okay();
}

auto multipart_decoder::finalize_headers() -> http::outcome {
    std::vector<std::string> unfolded;
    for (std::string& raw : lines_) {
        if (!raw.empty() && multipart_params::is_hspace(raw.front())) {
            if (unfolded.empty()) {
                return fail(http::outcome_code::invalid_argument, "part header block starts with a fold");
            }
            unfolded.back() += ' ';
            unfolded.back() += multipart_params::trim(raw);
        } else {
            unfolded.push_back(std::move(raw));
        }
    }
    for (const std::string& line : unfolded) {
        const http::outcome recorded = record_header(line);
        if (!recorded.ok()) return recorded;
    }
    if (!saw_disposition_ || !has_name_) {
        return fail(http::outcome_code::invalid_argument, "part without a usable Content-Disposition");
    }
    return http::outcome::okay();
}

auto multipart_decoder::record_header(const std::string& line) -> http::outcome {
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        return fail(http::outcome_code::invalid_argument, "part header line without a colon");
    }
    const std::string_view name = multipart_params::trim(std::string_view(line).substr(0, colon));
    const std::string_view value = multipart_params::trim(std::string_view(line).substr(colon + 1));
    if (!saw_disposition_ && auth_text::ascii_iequal(name, "content-disposition")) {
        return parse_disposition(value);
    }
    if (content_type_.empty() && auth_text::ascii_iequal(name, "content-type")) {
        content_type_ = std::string(value);
    } else if (transfer_encoding_.empty() && auth_text::ascii_iequal(name, "transfer-encoding")) {
        transfer_encoding_ = std::string(value);
    }
    return http::outcome::okay();
}

auto multipart_decoder::parse_disposition(std::string_view value) -> http::outcome {
    saw_disposition_ = true;
    std::string type;
    std::vector<std::pair<std::string, std::string>> params;
    const http::outcome parsed = multipart_params::parse_type_params(value, type, params);
    if (!parsed.ok()) return parsed;
    for (const std::pair<std::string, std::string>& param : params) {
        if (!has_name_ && auth_text::ascii_iequal(param.first, "name")) {
            name_ = param.second;
            has_name_ = true;
        } else if (!has_filename_ && auth_text::ascii_iequal(param.first, "filename")) {
            filename_ = param.second;
            has_filename_ = true;
        }
    }
    return http::outcome::okay();
}

auto multipart_decoder::fail(http::outcome_code code, const char* what) -> http::outcome {
    failure_ = http::outcome(code, std::string("multipart_decoder: ") + what);
    failed_ = true;
    return failure_;
}

auto multipart_decoder::fail_with(const http::outcome& reason) -> http::outcome {
    failure_ = reason;
    failed_ = true;
    return failure_;
}

}  // namespace httpserver::detail
