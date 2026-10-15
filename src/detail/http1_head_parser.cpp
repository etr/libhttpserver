/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>

#include <httpserver/detail/http1_head_parser.hpp>

namespace httpserver::detail {

http1_head_parser::http1_head_parser(const http1_head_budget& budget, server::resource_budget scope) noexcept
    : budget_(budget), bytes_charge_(scope, server::resource::header_bytes), fields_charge_(scope, server::resource::header_fields) {}

}  // namespace httpserver::detail

namespace httpserver::detail::detail_head {

bool contains_ctl(std::string_view v) noexcept {
    for (const char c : v) {
        if (is_ctl(c)) return true;
    }
    return false;
}

bool contains_field_value_ctl(std::string_view v) noexcept {
    for (const char c : v) {
        if (c != '\t' && is_ctl(c)) return true;
    }
    return false;
}

std::string_view trim_ows(std::string_view v) noexcept {
    while (!v.empty() && is_ows(v.front())) v.remove_prefix(1);
    while (!v.empty() && is_ows(v.back())) v.remove_suffix(1);
    return v;
}

bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

}  // namespace httpserver::detail::detail_head

namespace httpserver::detail {

void http1_head_parser::feed(std::string_view bytes) {
    if (state_ == http1_head_state::failed) return;
    if (!bytes.empty() && !admit(bytes)) return;
    if (state_ == http1_head_state::complete) return;
    scan_and_parse();
}

bool http1_head_parser::admit(std::string_view bytes) {
    // header_bytes is enforced on the prospective buffer size BEFORE
    // the append: a head may never outgrow max_head_bytes. Once a head
    // is complete, at most one further head of pipelined residue may
    // buffer behind it (take() re-arms the budget for the next head).
    const std::size_t cap = state_ == http1_head_state::complete ? head_end_ + budget_.max_head_bytes : budget_.max_head_bytes;
    if (bytes.size() > cap - buffer_.size()) {
        return fail(http::outcome_code::limit_exceeded, "header_bytes budget exhausted");
    }
    if (!bytes_charge_.grow(3 * bytes.size())) return fail(http::outcome_code::limit_exceeded, "shared header_bytes capacity exhausted");
    const std::size_t base = buffer_.size();
    buffer_.append(bytes);
    // S5: in a well-formed head every LF is the second byte of a CRLF
    // pair, so an LF whose preceding buffered byte is not CR is a bare
    // LF — malformed no matter what arrives later, and detectable the
    // moment it is fed. buffer_ always starts at a head boundary, so
    // the byte before the appended region (the tail of the previous
    // feed, or the final byte of a completed head when buffering
    // residue) is the correct predecessor to consult across splits.
    char previous = base > 0 ? buffer_[base - 1] : '\0';
    for (const char c : bytes) {
        if (c == '\n') {
            if (previous != '\r') {
                return fail(http::outcome_code::protocol_error, "bare LF in head");
            }
            ++lf_count_;
        }
        previous = c;
    }
    // Streaming LF counter: a valid complete head carries at most
    // max_fields + 3 line terminators (the optional skipped leading
    // blank line, the request line, the field lines, the empty line).
    // More LFs without a terminator means the field budget is already
    // lost, so reject here — before the parse could append any field —
    // and let parse time re-count exactly.
    if (state_ != http1_head_state::complete && lf_count_ > budget_.max_fields + 3) {
        return fail(http::outcome_code::limit_exceeded, "header_fields budget exhausted");
    }
    return true;
}

http::request_head http1_head_parser::take(resource_charge* bytes, resource_charge* fields) {
    auto retained_bytes = bytes_charge_.take(3 * head_end_);
    auto retained_fields = fields_charge_.take(fields_charge_.units());
    if (bytes) *bytes = std::move(retained_bytes);
    if (fields) *fields = std::move(retained_fields);
    http::request_head out = std::move(head_);
    head_ = http::request_head();
    buffer_.erase(0, head_end_);
    if (buffer_.empty()) std::string().swap(buffer_);
    head_end_ = 0;
    next_scan_ = 0;
    lf_count_ = 0;
    state_ = http1_head_state::empty;
    scan_and_parse();
    return out;
}

std::string_view http1_head_parser::residue_view() const noexcept {
    if (state_ != http1_head_state::complete) return std::string_view();
    return std::string_view(buffer_).substr(head_end_);
}

void http1_head_parser::consume_residue(std::size_t n) {
    if (state_ != http1_head_state::complete) return;
    const auto consumed = std::min(n, buffer_.size() - head_end_);
    buffer_.erase(head_end_, consumed);
    bytes_charge_.shrink(3 * consumed);
}

bool http1_head_parser::fail(http::outcome_code code, std::string message) {
    failure_ = http::outcome(code, "http1_head_parser: " + std::move(message));
    close_ = code == http::outcome_code::limit_exceeded ? http1_close_policy::respond_then_close : http1_close_policy::close_now;
    state_ = http1_head_state::failed;
    return false;
}

void http1_head_parser::scan_and_parse() {
    const std::size_t found = buffer_.find("\r\n\r\n", next_scan_);
    if (found == std::string::npos) {
        // Everything up to size-3 is ruled out; a terminator may still
        // start three bytes back once more bytes arrive.
        const std::size_t size = buffer_.size();
        next_scan_ = size >= 3 ? size - 3 : 0;
        state_ = buffer_.empty() ? http1_head_state::empty : http1_head_state::partial;
        return;
    }
    head_end_ = found + 4;
    parse_buffered_head();
}

void http1_head_parser::parse_buffered_head() {
    const std::string_view head(buffer_.data(), head_end_);
    std::size_t pos = 0;
    bool request_line_seen = false;
    bool blank_skipped = false;
    while (pos < head_end_) {
        const std::size_t crlf = buffer_.find("\r\n", pos);
        if (crlf == std::string::npos || crlf >= head_end_) {
            // Unreachable when head_end_ marks the first CRLFCRLF, but
            // a guard keeps a future scan change from slicing past it.
            fail(http::outcome_code::protocol_error, "head line does not end with CRLF");
            return;
        }
        const std::string_view line = head.substr(pos, crlf - pos);
        if (line.empty()) {
            // At most ONE leading bare CRLF is ignored (RFC 9112
            // section 2.2). Migration note (delta 6): v2 tolerated
            // arbitrarily many leading blank lines.
            if (!request_line_seen) {
                if (blank_skipped) {
                    fail(http::outcome_code::protocol_error, "second leading empty line");
                    return;
                }
                blank_skipped = true;
            } else {
                break;  // the empty line ends the field section
            }
        } else if (!request_line_seen) {
            if (!parse_request_line(line)) return;
            request_line_seen = true;
        } else {
            if (!parse_field_line(line)) return;
        }
        pos = crlf + 2;
    }
    state_ = http1_head_state::complete;
}

bool http1_head_parser::parse_request_line(std::string_view line) {
    // No NUL or other CTL byte in the request line. Migration note
    // (delta 3): v2 truncated at NUL.
    if (detail_head::contains_ctl(line)) {
        return fail(http::outcome_code::protocol_error, "CTL byte in request-line");
    }
    const std::size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) {
        return fail(http::outcome_code::protocol_error, "request-line is not three SP-separated elements");
    }
    const std::string_view rest = line.substr(sp1 + 1);
    const std::size_t sp2 = rest.find(' ');
    if (sp2 == std::string_view::npos) {
        return fail(http::outcome_code::protocol_error, "request-line is not three SP-separated elements");
    }
    const std::optional<http::method> parsed_method = http::method::parse(line.substr(0, sp1));
    if (!parsed_method) {
        return fail(http::outcome_code::protocol_error, "method is not an RFC 9110 token");
    }
    const std::string_view version = rest.substr(sp2 + 1);
    const std::optional<http::protocol> parsed_protocol = http::parse(version);
    if (!parsed_protocol || (*parsed_protocol != http::protocol::http_1_0 && *parsed_protocol != http::protocol::http_1_1)) {
        return fail(http::outcome_code::protocol_error, "version is not HTTP/1.0 or HTTP/1.1");
    }
    head_.request_method = *parsed_method;
    head_.request_protocol = *parsed_protocol;
    return apply_target(rest.substr(0, sp2));
}

bool http1_head_parser::apply_target(std::string_view target) {
    const http::outcome derived = http1_target::derive_route_path(head_.request_method, target, head_.route_path);
    if (!derived.ok()) return fail(derived.code(), derived.message());
    // REQ-019: the received target is stored byte-exact and never
    // rewritten; route_path is the derived matching input.
    head_.raw_target = std::string(target);
    return true;
}

bool http1_head_parser::parse_field_line(std::string_view line) {
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return fail(http::outcome_code::protocol_error, "field line has no colon");
    }
    // H1/H2/H3: the name must be an RFC 9110 token. That single check
    // rejects the whitespace-before-colon form "Host : x" (RFC 9112
    // section 5.1 MUST), any CTL in the name, and every obs-fold
    // continuation line (it would start with SP/HTAB, which no leading
    // token byte may be; RFC 9112 section 5.2 MUST).
    const std::string_view name = line.substr(0, colon);
    if (!http::detail::is_token(name)) {
        return fail(http::outcome_code::protocol_error, "field name is not an RFC 9110 token");
    }
    // H5/H8: no CTL in the value except HTAB, which is OWS. Migration
    // note (delta 3): v2 truncated at NUL; v3 rejects.
    const std::string_view value = detail_head::trim_ows(line.substr(colon + 1));
    if (detail_head::contains_field_value_ctl(value)) {
        return fail(http::outcome_code::protocol_error, "CTL byte in field value");
    }
    // H7 exact re-count at parse time: the streaming counter is
    // conservative (it cannot distinguish a field line from the
    // optional skipped leading blank), so the authoritative
    // per-occurrence check runs here — still BEFORE the append that
    // would allocate the next field occurrence.
    if (head_.head_fields.size() >= budget_.max_fields) {
        return fail(http::outcome_code::limit_exceeded, "header_fields budget exhausted");
    }
    if (!fields_charge_.grow(1)) return fail(http::outcome_code::limit_exceeded, "shared header_fields capacity exhausted");
    head_.head_fields.append(name, value);
    return true;
}

}  // namespace httpserver::detail
