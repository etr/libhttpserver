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

// TASK-117: the strict incremental multipart/form-data wire decoder
// (PRD-V3N-REQ-021, DR-V3-001). NOT part of the installed surface.
//
// Wire semantics (RFC 2046 framing, v2's MHD post processor behavior
// within cap): the body is an optional preamble, then parts separated
// by CRLF "--" boundary, ended by CRLF "--" boundary "--" and an
// optional epilogue. The delimiter match is case-SENSITIVE and
// line-anchored: a CRLF followed by the boundary plus anything other
// than "--", transport padding, or CRLF is DATA. Part headers are
// CRLF-terminated lines closed by one blank line; obs-fold
// continuations (a leading SP/HT) join the previous line's value.
// Content-Disposition provides the part identity: the FIRST
// Content-Disposition wins, its "name" parameter is required, its
// "filename" parameter distinguishes a file part, and parameter names
// are case-insensitive with quoted-string backslash escapes. The
// per-part Content-Type and Transfer-Encoding values (first wins) are
// carried to the part callback.
//
// The TASK-117 strictness deltas over v2 (migration-noted): a missing
// first or final boundary, a delimiter truncated at EOF, a header line
// without a colon or without CRLF, a part without a usable
// Content-Disposition, a malformed parameter list, and transport
// padding after a delimiter longer than the boundary plus 8 bytes are
// typed invalid_argument rejections, where MHD silently produced no
// parts (and, for padding, tolerated it unboundedly); the byte,
// part-count, per-part, and header-block budgets are typed
// limit_exceeded rejections, where v2's effective default was
// unbounded.
//
// Bounded by construction: pending_ never holds more than the incoming
// feed plus the holdback window (boundary + 4 bytes) in part_body, so
// the trailing delimiter candidate is never emitted as data, and a
// part's bytes are emitted only under the per-part budget check. The
// padding bound above keeps the same guarantee on the delimiter's
// trailing side: an unterminated padding run is rejected once it
// passes the bound, so it can never grow the window or the per-feed
// rescan unboundedly.
// Events see views of decoder-owned storage valid only inside the
// callback. State survives arbitrary feed boundaries, so streaming
// feeds and one-shot feeds decode identically. Rejections are sticky:
// a failed decoder refuses further feeds and finish() with the same
// typed outcome, and an events-visitor failure is adopted as the
// sticky failure (the driver layer turns it into the part abort).

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/forms_multipart.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_FORMS_MULTIPART_HPP_
#define SRC_HTTPSERVER_DETAIL_FORMS_MULTIPART_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/detail/auth_text.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

// The identity of one begun part, as the wire machine parsed it. The
// views address decoder-owned storage and are valid only inside the
// on_part_begin callback.
struct multipart_part_meta {
    std::string_view name;               // Content-Disposition name
    std::string_view filename;           // empty for a field part
    std::string_view content_type;       // part Content-Type value
    std::string_view transfer_encoding;  // part Transfer-Encoding value
};

// The pure wire machine's output side (no cleanup policy here; the
// driver layer adapts it to forms::part_sink).
class multipart_events {
 public:
    virtual ~multipart_events() = default;
    virtual http::outcome on_part_begin(const multipart_part_meta&) = 0;
    virtual http::outcome on_part_data(std::span<const std::byte>) = 0;
    virtual http::outcome on_part_end() = 0;
};

// Parameter-list syntax shared by the Content-Type boundary lookup and
// the Content-Disposition identity: "type; p=v; q="quoted"" with
// case-insensitive names and backslash escapes inside quoted strings.
namespace multipart_params {

inline bool is_hspace(char c) noexcept {
    return c == ' ' || c == '\t';
}

std::string_view trim(std::string_view v) noexcept;

std::size_t skip_hspace(std::string_view v,
                               std::size_t at) noexcept;

// Skips whitespace and empty ';' separators between parameters.
std::size_t skip_separators(std::string_view v,
                                   std::size_t at) noexcept;

// Reads one quoted-string value starting at the opening quote; @p at
// lands after the closing quote.
http::outcome read_quoted(std::string_view v, std::size_t& at,
                                 std::string& out);

http::outcome parse_value(std::string_view v, std::size_t& at,
                                 std::string& out);

// Reads one "name=value" parameter at @p at; @p at lands on the next
// separator.
http::outcome parse_param(std::string_view v, std::size_t& at,
                                 std::pair<std::string, std::string>& out);

// Splits @p value into its leading type token and its parameters.
http::outcome parse_type_params(
    std::string_view value, std::string& type,
    std::vector<std::pair<std::string, std::string>>& params);

}  // namespace multipart_params

// One RFC 2046 bchar: a bcharsnospace, or a space (which may not be
// the last character -- the caller checks that).
bool is_bchar(char c) noexcept;

// Validates one boundary per RFC 2046: 1..256 bchars, a space allowed
// anywhere but last. This is the driver-setup gate (a Content-Type
// with an invalid boundary never reaches the wire machine).
http::outcome validate_boundary(std::string_view boundary);

// Extracts and validates the boundary parameter of a Content-Type
// value (parameter names case-insensitive; token or quoted value).
// A missing Content-Type or a missing/invalid boundary parameter is a
// typed invalid_argument.
http::outcome extract_boundary(
    std::optional<std::string_view> content_type, std::string& out);

// Incremental decoder for one multipart/form-data request body. Not
// thread-safe: the caller feeds it on the reader's thread.
class multipart_decoder {
 public:
    multipart_decoder(const std::string& boundary,
                      std::uint64_t max_total_bytes,
                      std::uint64_t max_parts, std::uint64_t max_part_bytes,
                      std::uint64_t max_part_header_bytes,
                      multipart_events& events) noexcept;

    // Decodes a prefix of the raw body under the byte budget: bytes up
    // to the budget are consumed first, the first byte past it trips
    // the typed failure (exactly-at-cap succeeds on the feed that ends
    // at the budget). After a failure every later feed returns the same
    // outcome and consumes nothing.
    http::outcome feed(std::span<const std::byte> wire);

    // Ends the body. An empty body is ok with zero parts (a bodyless
    // request); anything else that never reached the epilogue is the
    // typed missing-boundary rejection. Idempotent after a failure.
    http::outcome finish();

    std::uint64_t parts_completed() const noexcept {
        return parts_completed_;
    }

 private:
    enum class state : std::uint8_t {
        preamble, part_headers, part_body, epilogue,
    };
    enum class verdict : std::uint8_t {
        need_more, final_boundary, next_part, not_delimiter,
        padding_reject,
    };
    enum class crlf_verdict : std::uint8_t {
        yes, no, need_more,
    };

    // CRLF at @p k (a complete line end)?
    crlf_verdict crlf_at(std::size_t k) const noexcept;

    // "--" at @p j: the final boundary.
    verdict decide_dash(std::size_t j) noexcept;

    // What follows the boundary at @p j: the closing "--", transport
    // padding then CRLF (next part), or CRLF (next part). Anything
    // else means this candidate is data. On a delimiter verdict,
    // follow_end_ lands after the consumed delimiter bytes. Transport
    // padding (optional LWSP) is tolerated only up to the documented
    // strictness bound -- the boundary length plus 8 bytes: a longer
    // run, terminated or not, is the typed padding_reject (sticky
    // malformed), so the pending window never grows with an
    // unterminated padding run and each feed rescans at most
    // bound-many held bytes (the streaming invariant; the migration
    // note records the delta over MHD's unbounded tolerance).
    verdict decide_follow(std::size_t j);

    // Drives the current state until it needs more bytes.
    http::outcome pump();

    // Retains at most @p keep tail bytes of the pending window.
    void hold_tail(std::size_t keep);

    // Preamble: drain everything (never stored) up to the first
    // delimiter.
    http::outcome pump_preamble();

    // Part headers: accumulate CRLF-terminated lines until the blank
    // line, under the header-block budget.
    http::outcome pump_headers();

    // Part body: emit data, holding back the delimiter window.
    http::outcome pump_body();

    // Emits the data before a decided delimiter, closes the part, and
    // transitions to the epilogue or the next part's headers.
    http::outcome end_part_at(std::size_t data_len, bool final_boundary);

    // Emits @p n pending bytes as part data under the per-part budget.
    http::outcome emit_data(std::size_t n);

    // Resets the per-part header state.
    void begin_headers() noexcept;

    // The blank line arrived: unfold, validate, and begin the part.
    http::outcome begin_part();

    // Unfolds obs-fold lines and records the interesting headers
    // (first occurrence wins for each).
    http::outcome finalize_headers();

    // One unfolded header line into the part identity.
    http::outcome record_header(const std::string& line);

    // Content-Disposition value into name_/filename_ (first of each
    // wins; any disposition type accepted, matching the v2 parser).
    http::outcome parse_disposition(std::string_view value);

    // Records the typed, sticky rejection; returns it.
    http::outcome fail(http::outcome_code code, const char* what);

    // Adopts a visitor failure as the sticky rejection.
    http::outcome fail_with(const http::outcome& reason);

    const std::string needle_;  // CRLF "--" boundary
    // The transport-padding bound after one delimiter: the boundary
    // length plus 8 bytes (needle_ is the boundary plus 4).
    const std::size_t max_follow_padding_;
    const std::uint64_t max_total_bytes_;
    const std::uint64_t max_parts_;
    const std::uint64_t max_part_bytes_;
    const std::uint64_t max_part_header_bytes_;
    multipart_events& events_;
    std::string pending_;
    std::vector<std::string> lines_;
    std::string name_;
    std::string filename_;
    std::string content_type_;
    std::string transfer_encoding_;
    http::outcome failure_;
    std::uint64_t bytes_seen_ = 0;
    std::uint64_t block_bytes_ = 0;
    std::uint64_t part_bytes_ = 0;
    std::uint64_t parts_begun_ = 0;
    std::uint64_t parts_completed_ = 0;
    std::size_t follow_end_ = 0;
    state state_ = state::preamble;
    bool saw_disposition_ = false;
    bool has_name_ = false;
    bool has_filename_ = false;
    bool need_more_ = false;
    bool failed_ = false;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_FORMS_MULTIPART_HPP_
