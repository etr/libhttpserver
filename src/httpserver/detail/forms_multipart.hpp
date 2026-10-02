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

inline std::string_view trim(std::string_view v) noexcept {
    while (!v.empty() && is_hspace(v.front())) v.remove_prefix(1);
    while (!v.empty() && is_hspace(v.back())) v.remove_suffix(1);
    return v;
}

inline std::size_t skip_hspace(std::string_view v,
                               std::size_t at) noexcept {
    while (at < v.size() && is_hspace(v[at])) ++at;
    return at;
}

// Skips whitespace and empty ';' separators between parameters.
inline std::size_t skip_separators(std::string_view v,
                                   std::size_t at) noexcept {
    while (at < v.size()
           && (is_hspace(v[at]) || v[at] == ';')) {
        ++at;
    }
    return at;
}

// Reads one quoted-string value starting at the opening quote; @p at
// lands after the closing quote.
inline http::outcome read_quoted(std::string_view v, std::size_t& at,
                                 std::string& out) {
    ++at;  // the opening quote
    for (;;) {
        if (at >= v.size()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "multipart_decoder: unterminated quoted string");
        }
        const char c = v[at];
        if (c == '"') {
            ++at;
            return http::outcome::okay();
        }
        if (c == '\\') {
            if (at + 1 >= v.size()) {
                return http::outcome(
                    http::outcome_code::invalid_argument,
                    "multipart_decoder: truncated quoted-pair");
            }
            out.push_back(v[at + 1]);
            at += 2;
            continue;
        }
        out.push_back(c);
        ++at;
    }
}

inline http::outcome parse_value(std::string_view v, std::size_t& at,
                                 std::string& out) {
    if (at < v.size() && v[at] == '"') {
        return read_quoted(v, at, out);
    }
    const std::size_t start = at;
    while (at < v.size() && v[at] != ';') ++at;
    out.assign(trim(v.substr(start, at - start)));
    return http::outcome::okay();
}

// Reads one "name=value" parameter at @p at; @p at lands on the next
// separator.
inline http::outcome parse_param(std::string_view v, std::size_t& at,
                                 std::pair<std::string, std::string>& out) {
    const std::size_t name_start = at;
    while (at < v.size() && v[at] != '=' && v[at] != ';'
           && !is_hspace(v[at])) {
        ++at;
    }
    if (at == name_start) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "multipart_decoder: empty parameter name");
    }
    out.first.assign(v.substr(name_start, at - name_start));
    at = skip_hspace(v, at);
    if (at >= v.size() || v[at] != '=') {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "multipart_decoder: parameter without '='");
    }
    ++at;
    at = skip_hspace(v, at);
    return parse_value(v, at, out.second);
}

// Splits @p value into its leading type token and its parameters.
inline http::outcome parse_type_params(
    std::string_view value, std::string& type,
    std::vector<std::pair<std::string, std::string>>& params) {
    value = trim(value);
    const std::size_t semi = value.find(';');
    type.assign(value.substr(0, semi == std::string_view::npos
                                       ? value.size()
                                       : semi));
    if (type.empty()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "multipart_decoder: empty media type");
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

}  // namespace multipart_params

// One RFC 2046 bchar: a bcharsnospace, or a space (which may not be
// the last character -- the caller checks that).
inline bool is_bchar(char c) noexcept {
    constexpr std::string_view k_special = "'()+_,-./:=? ";
    if (k_special.find(c) != std::string_view::npos) return true;
    const bool digit = c >= '0' && c <= '9';
    const bool upper = c >= 'A' && c <= 'Z';
    const bool lower = c >= 'a' && c <= 'z';
    return digit || upper || lower;
}

// Validates one boundary per RFC 2046: 1..256 bchars, a space allowed
// anywhere but last. This is the driver-setup gate (a Content-Type
// with an invalid boundary never reaches the wire machine).
inline http::outcome validate_boundary(std::string_view boundary) {
    if (boundary.empty() || boundary.size() > 256) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "multipart boundary length must be 1..256");
    }
    for (const char c : boundary) {
        if (!is_bchar(c)) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "multipart boundary has a character outside RFC 2046 "
                "bchars");
        }
    }
    if (boundary.back() == ' ') {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "multipart boundary must not end with a space");
    }
    return http::outcome::okay();
}

// Extracts and validates the boundary parameter of a Content-Type
// value (parameter names case-insensitive; token or quoted value).
// A missing Content-Type or a missing/invalid boundary parameter is a
// typed invalid_argument.
inline http::outcome extract_boundary(
    std::optional<std::string_view> content_type, std::string& out) {
    if (!content_type.has_value()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "multipart_decoder: request carries no Content-Type");
    }
    std::string type;
    std::vector<std::pair<std::string, std::string>> params;
    const http::outcome parsed = multipart_params::parse_type_params(
        *content_type, type, params);
    if (!parsed.ok()) return parsed;
    for (const std::pair<std::string, std::string>& param : params) {
        if (auth_text::ascii_iequal(param.first, "boundary")) {
            const http::outcome valid =
                validate_boundary(param.second);
            if (!valid.ok()) return valid;
            out = param.second;
            return http::outcome::okay();
        }
    }
    return http::outcome(
        http::outcome_code::invalid_argument,
        "multipart_decoder: Content-Type carries no boundary");
}

// Incremental decoder for one multipart/form-data request body. Not
// thread-safe: the caller feeds it on the reader's thread.
class multipart_decoder {
 public:
    multipart_decoder(const std::string& boundary,
                      std::uint64_t max_total_bytes,
                      std::uint64_t max_parts, std::uint64_t max_part_bytes,
                      std::uint64_t max_part_header_bytes,
                      multipart_events& events) noexcept
        : needle_("\r\n--" + boundary),
          max_follow_padding_(needle_.size() + 4),
          max_total_bytes_(max_total_bytes), max_parts_(max_parts),
          max_part_bytes_(max_part_bytes),
          max_part_header_bytes_(max_part_header_bytes),
          events_(events) {
        // A virtual CRLF makes the stream-start delimiter (which needs
        // no preceding CRLF) the same needle as every later one.
        pending_.assign("\r\n", 2);
    }

    // Decodes a prefix of the raw body under the byte budget: bytes up
    // to the budget are consumed first, the first byte past it trips
    // the typed failure (exactly-at-cap succeeds on the feed that ends
    // at the budget). After a failure every later feed returns the same
    // outcome and consumes nothing.
    http::outcome feed(std::span<const std::byte> wire) {
        if (failed_) return failure_;
        if (wire.empty()) return http::outcome::okay();
        std::size_t usable = wire.size();
        bool over = false;
        if (bytes_seen_ + static_cast<std::uint64_t>(wire.size())
            > max_total_bytes_) {
            usable = static_cast<std::size_t>(
                max_total_bytes_ - bytes_seen_);
            over = true;
        }
        const char* raw = reinterpret_cast<const char*>(wire.data());
        pending_.append(raw, usable);
        bytes_seen_ += usable;
        const http::outcome pumped = pump();
        if (!pumped.ok()) return pumped;
        if (over) {
            return fail(http::outcome_code::limit_exceeded,
                        "raw body bytes budget exhausted");
        }
        return http::outcome::okay();
    }

    // Ends the body. An empty body is ok with zero parts (a bodyless
    // request); anything else that never reached the epilogue is the
    // typed missing-boundary rejection. Idempotent after a failure.
    http::outcome finish() {
        if (failed_) return failure_;
        if (state_ == state::epilogue) return http::outcome::okay();
        if (state_ == state::preamble && bytes_seen_ == 0) {
            return http::outcome::okay();
        }
        return fail(http::outcome_code::invalid_argument,
                    "multipart body ended without a final boundary");
    }

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
    crlf_verdict crlf_at(std::size_t k) const noexcept {
        if (k >= pending_.size()) return crlf_verdict::need_more;
        if (pending_[k] != '\r') return crlf_verdict::no;
        if (k + 1 >= pending_.size()) return crlf_verdict::need_more;
        return pending_[k + 1] == '\n' ? crlf_verdict::yes
                                       : crlf_verdict::no;
    }

    // "--" at @p j: the final boundary.
    verdict decide_dash(std::size_t j) noexcept {
        if (j + 1 >= pending_.size()) return verdict::need_more;
        if (pending_[j + 1] != '-') return verdict::not_delimiter;
        follow_end_ = j + 2;
        return verdict::final_boundary;
    }

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
    verdict decide_follow(std::size_t j) {
        if (j >= pending_.size()) return verdict::need_more;
        if (pending_[j] == '-') return decide_dash(j);
        std::size_t k = j;
        while (k < pending_.size()
               && multipart_params::is_hspace(pending_[k])) {
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

    // Drives the current state until it needs more bytes.
    http::outcome pump() {
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

    // Retains at most @p keep tail bytes of the pending window.
    void hold_tail(std::size_t keep) {
        if (pending_.size() > keep) {
            pending_.erase(0, pending_.size() - keep);
        }
    }

    // Preamble: drain everything (never stored) up to the first
    // delimiter.
    http::outcome pump_preamble() {
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

    // Part headers: accumulate CRLF-terminated lines until the blank
    // line, under the header-block budget.
    http::outcome pump_headers() {
        need_more_ = false;
        for (;;) {
            const std::size_t nl = pending_.find('\n');
            if (nl == std::string::npos) {
                // An unterminated line can never fit the block budget.
                if (block_bytes_ + pending_.size()
                    > max_part_header_bytes_) {
                    return fail(http::outcome_code::limit_exceeded,
                                "part header block budget exhausted");
                }
                need_more_ = true;
                return http::outcome::okay();
            }
            block_bytes_ += nl + 1;
            if (block_bytes_ > max_part_header_bytes_) {
                return fail(http::outcome_code::limit_exceeded,
                            "part header block budget exhausted");
            }
            std::string_view line(pending_.data(), nl);
            if (line.empty() || line.back() != '\r') {
                return fail(http::outcome_code::invalid_argument,
                            "part header line without CRLF");
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

    // Part body: emit data, holding back the delimiter window.
    http::outcome pump_body() {
        need_more_ = false;
        std::size_t from = 0;
        for (;;) {
            const std::size_t p = pending_.find(needle_, from);
            if (p == std::string::npos) {
                const std::size_t n =
                    pending_.size() > needle_.size()
                        ? pending_.size() - needle_.size()
                        : 0;
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

    // Emits the data before a decided delimiter, closes the part, and
    // transitions to the epilogue or the next part's headers.
    http::outcome end_part_at(std::size_t data_len, bool final_boundary) {
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

    // Emits @p n pending bytes as part data under the per-part budget.
    http::outcome emit_data(std::size_t n) {
        if (n == 0) return http::outcome::okay();
        if (part_bytes_ + n > max_part_bytes_) {
            return fail(http::outcome_code::limit_exceeded,
                        "part body bytes budget exhausted");
        }
        const char* raw = pending_.data();
        const http::outcome emitted = events_.on_part_data(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(raw), n));
        if (!emitted.ok()) return fail_with(emitted);
        part_bytes_ += n;
        return http::outcome::okay();
    }

    // Resets the per-part header state.
    void begin_headers() noexcept {
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

    // The blank line arrived: unfold, validate, and begin the part.
    http::outcome begin_part() {
        const http::outcome ready = finalize_headers();
        if (!ready.ok()) return ready;
        if (parts_begun_ >= max_parts_) {
            return fail(http::outcome_code::limit_exceeded,
                        "parts budget exhausted");
        }
        const http::outcome emitted = events_.on_part_begin(
            multipart_part_meta{name_, filename_, content_type_,
                                transfer_encoding_});
        if (!emitted.ok()) return fail_with(emitted);
        ++parts_begun_;
        state_ = state::part_body;
        part_bytes_ = 0;
        return http::outcome::okay();
    }

    // Unfolds obs-fold lines and records the interesting headers
    // (first occurrence wins for each).
    http::outcome finalize_headers() {
        std::vector<std::string> unfolded;
        for (std::string& raw : lines_) {
            if (!raw.empty()
                && multipart_params::is_hspace(raw.front())) {
                if (unfolded.empty()) {
                    return fail(http::outcome_code::invalid_argument,
                                "part header block starts with a fold");
                }
                unfolded.back() += ' ';
                unfolded.back()
                    += multipart_params::trim(raw);
            } else {
                unfolded.push_back(std::move(raw));
            }
        }
        for (const std::string& line : unfolded) {
            const http::outcome recorded = record_header(line);
            if (!recorded.ok()) return recorded;
        }
        if (!saw_disposition_ || !has_name_) {
            return fail(http::outcome_code::invalid_argument,
                        "part without a usable Content-Disposition");
        }
        return http::outcome::okay();
    }

    // One unfolded header line into the part identity.
    http::outcome record_header(const std::string& line) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) {
            return fail(http::outcome_code::invalid_argument,
                        "part header line without a colon");
        }
        const std::string_view name = multipart_params::trim(
            std::string_view(line).substr(0, colon));
        const std::string_view value = multipart_params::trim(
            std::string_view(line).substr(colon + 1));
        if (!saw_disposition_
            && auth_text::ascii_iequal(name, "content-disposition")) {
            return parse_disposition(value);
        }
        if (content_type_.empty()
            && auth_text::ascii_iequal(name, "content-type")) {
            content_type_ = std::string(value);
        } else if (transfer_encoding_.empty()
                   && auth_text::ascii_iequal(
                       name, "transfer-encoding")) {
            transfer_encoding_ = std::string(value);
        }
        return http::outcome::okay();
    }

    // Content-Disposition value into name_/filename_ (first of each
    // wins; any disposition type accepted, matching the v2 parser).
    http::outcome parse_disposition(std::string_view value) {
        saw_disposition_ = true;
        std::string type;
        std::vector<std::pair<std::string, std::string>> params;
        const http::outcome parsed = multipart_params::parse_type_params(
            value, type, params);
        if (!parsed.ok()) return parsed;
        for (const std::pair<std::string, std::string>& param : params) {
            if (!has_name_
                && auth_text::ascii_iequal(param.first, "name")) {
                name_ = param.second;
                has_name_ = true;
            } else if (!has_filename_
                       && auth_text::ascii_iequal(param.first,
                                                  "filename")) {
                filename_ = param.second;
                has_filename_ = true;
            }
        }
        return http::outcome::okay();
    }

    // Records the typed, sticky rejection; returns it.
    http::outcome fail(http::outcome_code code, const char* what) {
        failure_ = http::outcome(
            code, std::string("multipart_decoder: ") + what);
        failed_ = true;
        return failure_;
    }

    // Adopts a visitor failure as the sticky rejection.
    http::outcome fail_with(const http::outcome& reason) {
        failure_ = reason;
        failed_ = true;
        return failure_;
    }

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
