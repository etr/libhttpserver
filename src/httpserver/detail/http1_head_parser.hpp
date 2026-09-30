/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// Strict HTTP/1 request-head parser for the native engine (TASK-105,
// PRD-V3N-REQ-004/017/019, DR-V3-006). NOT part of the installed
// surface; consumers cannot reach it through the public umbrella.
//
// Engine boundary: this parser owns request-head syntax only — the
// start line and the field lines, their whitespace and terminator
// strictness, and the byte/field budgets. Framing semantics belong to
// the connection engine: Transfer-Encoding vs Content-Length
// reconciliation, keep-alive and pipelining policy, header read
// timeouts, Expect: 100-continue, and responses before close. The
// parser accepts syntactically valid TE/CL combinations and exposes
// octets past the head terminator as residue so the engine can frame
// them.
//
// Strictness deltas vs the v2 (libmicrohttpd-backed) parser, all
// pre-approved for v3 and pinned by test/unit/http1_parser_test.cpp,
// are recorded as migration notes at each rejection site:
//   1. lone-LF and bare-CR termination: tolerated by v2, rejected here.
//   2. invalid percent-escapes: passed through by v2, rejected here.
//   3. NUL inside the target or a field line: v2 truncated at NUL,
//      rejected here.
//   4. empty request-target: v2 canonicalized "" to "/", rejected here.
//   5. '+' decoded as space in the route path: preserved from v2
//      (pinned at the decode site).
//   6. leading blank lines: v2 tolerated arbitrarily many, at most one
//      bare CRLF is skipped here (RFC 9112 section 2.2).

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_head_parser.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_HEAD_PARSER_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_HEAD_PARSER_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/detail/http1_head_limits.hpp>
#include <httpserver/detail/http1_target.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>

namespace httpserver {

namespace detail {

// Close posture the engine must take after a rejected head. none for a
// head that parsed fine; respond_then_close for a limit violation (the
// engine MAY emit 400/431 first, then MUST close); close_now for
// malformed syntax (the header state is untrustworthy; close without
// resync).
enum class http1_close_policy : std::uint8_t {
    none,
    respond_then_close,
    close_now,
};

// Parser lifecycle. empty: nothing fed since construction/take();
// partial: the head is incomplete, feed more octets; complete: one full
// head is parsed, take() it; failed: a typed rejection is recorded and
// sticky (feed() becomes a no-op).
enum class http1_head_state : std::uint8_t {
    empty,
    partial,
    complete,
    failed,
};

// Incremental strict parser for "request-line CRLF *(field-line CRLF)
// CRLF". feed() accepts arbitrary chunk boundaries; the CRLFCRLF
// terminator scan is memoized, so bytewise feeds stay O(total bytes).
// Parsing happens only once the terminator has arrived: the head is
// then split on CRLF and every line is validated. On success the head
// is exposed via take(), which consumes the head octets and leaves any
// pipelined bytes buffered as residue; the next head continues from
// there without the parser owning keep-alive policy.
class http1_head_parser {
 public:
    explicit http1_head_parser(const http1_head_budget& budget) noexcept
        : budget_(budget) { }

    // Buffers more octets. Never loses bytes: after a complete head,
    // further octets accumulate as residue until take() consumes the
    // head. The byte and field budgets are enforced before any buffer
    // growth or field allocation. After a failure the parser is sticky
    // and feed() is a no-op.
    void feed(std::string_view bytes);

    http1_head_state state() const noexcept { return state_; }

    // none unless failed.
    http1_close_policy close_policy() const noexcept { return close_; }

    // The recorded rejection; ok() unless failed. Sticky once failed.
    const http::outcome& failure() const noexcept { return failure_; }

    // Moves the completed head out, consumes its octets from the buffer,
    // and returns to the empty state (immediately re-entering complete
    // when a pipelined head is already fully buffered). Valid only in
    // the complete state.
    http::request_head take();

    // Octets buffered past the head terminator; nonzero only in the
    // complete state.
    std::size_t residue_size() const noexcept;

 private:
    // Records a typed rejection (sticky). Returns false so callers can
    // write `return fail(...)`.
    bool fail(http::outcome_code code, std::string message);

    // Budget admission for one feed: enforces header_bytes on the
    // prospective buffer size BEFORE the append and header_fields via
    // the streaming LF counter BEFORE any parse could append a field.
    bool admit(std::string_view bytes);

    // Resumes the memoized terminator scan and parses the head once the
    // CRLFCRLF has fully arrived.
    void scan_and_parse();

    // Splits the buffered head on CRLF and validates it line by line.
    void parse_buffered_head();

    // Parses "method SP request-target SP HTTP-version".
    bool parse_request_line(std::string_view line);

    // Form-selects and validates the request-target, stores it
    // byte-exact, and derives route_path (http1_target.hpp).
    bool apply_target(std::string_view target);

    // Parses one "name: value" field line.
    bool parse_field_line(std::string_view line);

    http1_head_budget budget_;
    std::string buffer_;
    http::request_head head_;
    http::outcome failure_;
    http1_head_state state_ = http1_head_state::empty;
    http1_close_policy close_ = http1_close_policy::none;
    std::size_t head_end_ = 0;   // end of the completed head inside buffer_
    std::size_t next_scan_ = 0;  // first offset not yet ruled out as a terminator start
    std::size_t lf_count_ = 0;   // LF bytes buffered for the head so far
};

namespace detail_head {

// CTL per RFC 9110: 0x00-0x1F and 0x7F.
constexpr bool is_ctl(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u <= 0x1F || u == 0x7F;
}

// OWS per RFC 9110: SP or HTAB.
constexpr bool is_ows(char c) noexcept {
    return c == ' ' || c == '\t';
}

// True iff any byte of v is a CTL.
inline bool contains_ctl(std::string_view v) noexcept {
    for (const char c : v) {
        if (is_ctl(c)) return true;
    }
    return false;
}

// True iff any byte of v is a CTL other than HTAB: field values may
// carry HTAB as interior OWS.
inline bool contains_field_value_ctl(std::string_view v) noexcept {
    for (const char c : v) {
        if (c != '\t' && is_ctl(c)) return true;
    }
    return false;
}

// Trims leading and trailing OWS; interior OWS is preserved verbatim.
inline std::string_view trim_ows(std::string_view v) noexcept {
    while (!v.empty() && is_ows(v.front())) v.remove_prefix(1);
    while (!v.empty() && is_ows(v.back())) v.remove_suffix(1);
    return v;
}

}  // namespace detail_head

inline void http1_head_parser::feed(std::string_view bytes) {
    if (state_ == http1_head_state::failed) return;
    if (!bytes.empty() && !admit(bytes)) return;
    if (state_ == http1_head_state::complete) return;
    scan_and_parse();
}

inline bool http1_head_parser::admit(std::string_view bytes) {
    // header_bytes is enforced on the prospective buffer size BEFORE
    // the append: a head may never outgrow max_head_bytes. Once a head
    // is complete, at most one further head of pipelined residue may
    // buffer behind it (take() re-arms the budget for the next head).
    const std::size_t cap = state_ == http1_head_state::complete
                                ? head_end_ + budget_.max_head_bytes
                                : budget_.max_head_bytes;
    if (bytes.size() > cap - buffer_.size()) {
        return fail(http::outcome_code::limit_exceeded,
                    "header_bytes budget exhausted");
    }
    buffer_.append(bytes);
    // Streaming LF counter: a valid complete head carries at most
    // max_fields + 2 line terminators (request line, field lines, the
    // empty line). More LFs without a terminator means the field budget
    // is already lost, so reject here — before the parse could append
    // any field — and let parse time re-count exactly.
    for (const char c : bytes) {
        if (c == '\n') ++lf_count_;
    }
    if (state_ != http1_head_state::complete
            && lf_count_ > budget_.max_fields + 2) {
        return fail(http::outcome_code::limit_exceeded,
                    "header_fields budget exhausted");
    }
    return true;
}

inline http::request_head http1_head_parser::take() {
    http::request_head out = std::move(head_);
    head_ = http::request_head();
    buffer_.erase(0, head_end_);
    head_end_ = 0;
    next_scan_ = 0;
    lf_count_ = 0;
    state_ = http1_head_state::empty;
    scan_and_parse();
    return out;
}

inline std::size_t http1_head_parser::residue_size() const noexcept {
    return state_ == http1_head_state::complete
               ? buffer_.size() - head_end_
               : 0;
}

inline bool http1_head_parser::fail(http::outcome_code code,
                                    std::string message) {
    failure_ = http::outcome(code,
                             "http1_head_parser: " + std::move(message));
    close_ = code == http::outcome_code::limit_exceeded
                 ? http1_close_policy::respond_then_close
                 : http1_close_policy::close_now;
    state_ = http1_head_state::failed;
    return false;
}

inline void http1_head_parser::scan_and_parse() {
    const std::size_t found = buffer_.find("\r\n\r\n", next_scan_);
    if (found == std::string::npos) {
        // Everything up to size-3 is ruled out; a terminator may still
        // start three bytes back once more bytes arrive.
        const std::size_t size = buffer_.size();
        next_scan_ = size >= 3 ? size - 3 : 0;
        state_ = buffer_.empty() ? http1_head_state::empty
                                 : http1_head_state::partial;
        return;
    }
    head_end_ = found + 4;
    parse_buffered_head();
}

inline void http1_head_parser::parse_buffered_head() {
    const std::string_view head(buffer_.data(), head_end_);
    std::size_t pos = 0;
    bool request_line_seen = false;
    bool blank_skipped = false;
    while (pos < head_end_) {
        const std::size_t crlf = buffer_.find("\r\n", pos);
        if (crlf == std::string::npos || crlf >= head_end_) {
            // Unreachable when head_end_ marks the first CRLFCRLF, but
            // a guard keeps a future scan change from slicing past it.
            fail(http::outcome_code::protocol_error,
                 "head line does not end with CRLF");
            return;
        }
        const std::string_view line = head.substr(pos, crlf - pos);
        if (line.empty()) {
            // At most ONE leading bare CRLF is ignored (RFC 9112
            // section 2.2). Migration note (delta 6): v2 tolerated
            // arbitrarily many leading blank lines.
            if (!request_line_seen) {
                if (blank_skipped) {
                    fail(http::outcome_code::protocol_error,
                         "second leading empty line");
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

inline bool http1_head_parser::parse_request_line(std::string_view line) {
    // No NUL or other CTL byte in the request line. Migration note
    // (delta 3): v2 truncated at NUL.
    if (detail_head::contains_ctl(line)) {
        return fail(http::outcome_code::protocol_error,
                    "CTL byte in request-line");
    }
    const std::size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) {
        return fail(http::outcome_code::protocol_error,
                    "request-line is not three SP-separated elements");
    }
    const std::string_view rest = line.substr(sp1 + 1);
    const std::size_t sp2 = rest.find(' ');
    if (sp2 == std::string_view::npos) {
        return fail(http::outcome_code::protocol_error,
                    "request-line is not three SP-separated elements");
    }
    const std::optional<http::method> parsed_method =
        http::method::parse(line.substr(0, sp1));
    if (!parsed_method) {
        return fail(http::outcome_code::protocol_error,
                    "method is not an RFC 9110 token");
    }
    const std::string_view version = rest.substr(sp2 + 1);
    const std::optional<http::protocol> parsed_protocol =
        http::parse(version);
    if (!parsed_protocol || (*parsed_protocol != http::protocol::http_1_0
                             && *parsed_protocol
                                    != http::protocol::http_1_1)) {
        return fail(http::outcome_code::protocol_error,
                    "version is not HTTP/1.0 or HTTP/1.1");
    }
    head_.request_method = *parsed_method;
    head_.request_protocol = *parsed_protocol;
    return apply_target(rest.substr(0, sp2));
}

inline bool http1_head_parser::apply_target(std::string_view target) {
    const http::outcome derived = http1_target::derive_route_path(
        head_.request_method, target, head_.route_path);
    if (!derived.ok()) return fail(derived.code(), derived.message());
    // REQ-019: the received target is stored byte-exact and never
    // rewritten; route_path is the derived matching input.
    head_.raw_target = std::string(target);
    return true;
}

inline bool http1_head_parser::parse_field_line(std::string_view line) {
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return fail(http::outcome_code::protocol_error,
                    "field line has no colon");
    }
    // H1/H2/H3: the name must be an RFC 9110 token. That single check
    // rejects the whitespace-before-colon form "Host : x" (RFC 9112
    // section 5.1 MUST), any CTL in the name, and every obs-fold
    // continuation line (it would start with SP/HTAB, which no leading
    // token byte may be; RFC 9112 section 5.2 MUST).
    const std::string_view name = line.substr(0, colon);
    if (!http::detail::is_token(name)) {
        return fail(http::outcome_code::protocol_error,
                    "field name is not an RFC 9110 token");
    }
    // H5/H8: no CTL in the value except HTAB, which is OWS. Migration
    // note (delta 3): v2 truncated at NUL; v3 rejects.
    const std::string_view value =
        detail_head::trim_ows(line.substr(colon + 1));
    if (detail_head::contains_field_value_ctl(value)) {
        return fail(http::outcome_code::protocol_error,
                    "CTL byte in field value");
    }
    head_.head_fields.append(name, value);
    return true;
}

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_HEAD_PARSER_HPP_
