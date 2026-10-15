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
// Strictness deltas vs the v2 parser, all
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/detail/http1_head_limits.hpp>
#include <httpserver/detail/resource_charge.hpp>
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
    explicit http1_head_parser(const http1_head_budget& budget, server::resource_budget scope = {}) noexcept;

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
    http::request_head take(resource_charge* bytes = nullptr, resource_charge* fields = nullptr);

    // Octets buffered past the head terminator; nonzero only in the
    // complete state.
    std::size_t residue_size() const noexcept;

    // View of the same octets residue_size() counts: the buffered bytes
    // after the head terminator (a pipelined next head, or the leading
    // octets of this request's body once the framing mode says there is
    // one). Empty outside the complete state. The view is invalidated
    // by take(), feed(), and consume_residue().
    std::string_view residue_view() const noexcept;

    // Erases the leading @p n bytes of the buffered residue (clamped to
    // residue_size()). The engine drains a complete head's residue into
    // its body path here, so the drained body prefix never re-enters
    // the next head. No-op outside the complete state.
    void consume_residue(std::size_t n);

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
    resource_charge bytes_charge_, fields_charge_;
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
bool contains_ctl(std::string_view v) noexcept;

// True iff any byte of v is a CTL other than HTAB: field values may
// carry HTAB as interior OWS.
bool contains_field_value_ctl(std::string_view v) noexcept;

// Trims leading and trailing OWS; interior OWS is preserved verbatim.
std::string_view trim_ows(std::string_view v) noexcept;

constexpr char ascii_lower(const char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') ? static_cast<char>(u - 'A' + 'a') : c;
}

// ASCII case-insensitive equality (field-name and transfer-coding
// comparisons across the HTTP/1 engine).
bool ascii_iequals(std::string_view a, std::string_view b) noexcept;

}  // namespace detail_head







inline std::size_t http1_head_parser::residue_size() const noexcept {
    return state_ == http1_head_state::complete
               ? buffer_.size() - head_end_
               : 0;
}

















}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_HEAD_PARSER_HPP_
