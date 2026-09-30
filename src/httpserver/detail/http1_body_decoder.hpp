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

// TASK-106: incremental HTTP/1 body framing decoder (PRD-V3N-REQ-004/
// 017/021, DR-V3-006). NOT part of the installed surface.
//
// Turns the wire bytes that follow a parsed request head into a staged
// body stream plus final trailers, per the authoritative
// http1_body_mode decided once by the engine. Wire ownership stays with
// the caller: decode() consumes the maximal prefix it can frame AND
// stage and reports the count; octets past the message boundary (and
// everything a full staging queue refuses) are never consumed and are
// re-fed by the engine. The staging queue is the backpressure
// boundary — pull() is the consumption event that releases room, so an
// engine that reads from the socket only while staged_bytes() is below
// the cap never buffers an unbounded body.
//
// Chunked framing (RFC 9112 section 7.1) is grammar-exact: hex sizes
// with optional (strictly parsed, discarded) extensions, CRLF after
// every chunk, and trailer field lines that preserve order and repeats
// while refusing framing-, routing-, and security-sensitive names.
// Failures are typed, sticky, and carry a close posture; truncation is
// NOT a decoder failure — an incomplete body simply never completes,
// and message_complete()/length_remaining() let the engine map a
// premature EOF to its typed failure.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_body_decoder.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_BODY_DECODER_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_BODY_DECODER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/detail/http1_body_mode.hpp>
#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/server/budgets.hpp>

namespace httpserver {

namespace detail {

// Body-framing budgets, projected from the hierarchical server budget
// surface: the staging queue tracks body_buffer_bytes, the trailer
// field count tracks header_fields, and the trailer byte cap is an
// engine constant.
struct http1_body_budget {
    std::size_t max_staged_bytes = 8388608;   // body_buffer_bytes default
    std::size_t max_trailer_fields = 256;     // header_fields default
    std::size_t max_trailer_bytes = 65536;    // engine constant

    static http1_body_budget from_budget_limits(
        const server::budget_limits& limits) noexcept {
        http1_body_budget budget;
        budget.max_staged_bytes =
            limits.get(server::resource::body_buffer_bytes);
        budget.max_trailer_fields =
            limits.get(server::resource::header_fields);
        return budget;
    }
};

// Verdict of one decode() call. progressed: the staging queue is full —
// `consumed` wire bytes were framed; pull to release room. need_more:
// the wire ran out mid-framing — feed more (`consumed` reports what was
// framed in this call). complete: the message boundary was reached (or
// had been reached before). failed: sticky rejection recorded.
enum class http1_body_decode : std::uint8_t {
    progressed, need_more, complete, failed,
};

struct http1_body_progress {
    http1_body_decode kind = http1_body_decode::need_more;
    std::size_t consumed = 0;
};

// Incremental framing state machine for one request body. Not
// thread-safe: the engine feeds and pulls it on the connection thread.
class http1_body_decoder {
 public:
    http1_body_decoder(const http1_body_mode& mode, http1_body_budget budget)
        : budget_(budget), kind_(mode.kind), failure_(mode.failure) {
        switch (kind_) {
            case http1_body_kind::none:
                phase_ = phase::message_end;
                break;
            case http1_body_kind::length:
                length_remaining_ = mode.content_length;
                phase_ = mode.content_length == 0 ? phase::message_end
                                                  : phase::length;
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

    // Frames a prefix of `wire` into the staging queue and returns how
    // much was consumed. Idempotent after complete/failed: complete
    // reports {complete, 0}, failure reports {failed, 0}.
    http1_body_progress decode(std::string_view wire) {
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
        if (total.blocked) return {http1_body_decode::progressed,
                                   total.consumed};
        return {http1_body_decode::need_more, total.consumed};
    }

    // THE consumption event: copies staged bytes into `into` and
    // releases staging room for exactly the bytes copied. Verdict
    // order: failed, then data (an empty destination never receives a
    // zero-byte data pull), then end (message end with the queue
    // drained), then empty.
    detail::body_pull_result pull(std::span<std::byte> into) {
        if (failed_) return {detail::body_pull::failed, 0};
        if (!into.empty() && !staging_.empty()) {
            std::vector<std::byte>& front = staging_.front();
            const std::size_t n = std::min(into.size(), front.size());
            std::copy_n(front.begin(), n, into.begin());
            staged_total_ -= n;
            if (n == front.size()) {
                staging_.pop_front();
            } else {
                front.erase(front.begin(),
                            front.begin()
                                + static_cast<std::ptrdiff_t>(n));
            }
            return {detail::body_pull::data, n};
        }
        if (staging_.empty() && phase_ == phase::message_end) {
            return {detail::body_pull::end, 0};
        }
        return {detail::body_pull::empty, 0};
    }

    // Received trailers; final and stable once a pull returned end.
    const http::fields& trailers() const noexcept { return trailers_; }

    // The sticky rejection; ok() until a decode/pull reported failed.
    const http::outcome& failure() const noexcept { return failure_; }

    // Close posture of the sticky rejection.
    http1_close_policy close_policy() const noexcept {
        return close_policy_;
    }

    http1_body_kind kind() const noexcept { return kind_; }

    bool message_complete() const noexcept {
        return !failed_ && phase_ == phase::message_end;
    }

    // Body octets still expected under the current framing expectation
    // (the fixed length, or the current chunk); 0 once complete.
    std::uint64_t length_remaining() const noexcept {
        return length_remaining_;
    }

    std::size_t staged_bytes() const noexcept { return staged_total_; }

 private:
    // Framing phases; none and length(0) construct at message_end.
    enum class phase : std::uint8_t {
        length, chunk_size, chunk_data, chunk_data_end, trailer_line,
        message_end,
    };

    // Bytes one phase step consumed from the front of the wire, plus
    // whether the message boundary was reached or the staging queue is
    // full (blocked: pulling releases room, more wire will not help).
    struct absorbed {
        std::size_t consumed = 0;
        bool complete = false;
        bool blocked = false;
    };

    // Runs phase steps until the wire is exhausted or the decode must
    // stop: message complete, sticky failure, or no progress (a line
    // phase out of wire, or the staging queue full).
    absorbed absorb_loop(std::string_view wire) {
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

    absorbed absorb_phase(std::string_view wire) {
        switch (phase_) {
            case phase::length: return decode_length(wire);
            case phase::chunk_size: return decode_chunk_size(wire);
            case phase::chunk_data: return decode_chunk_data(wire);
            case phase::chunk_data_end: return decode_chunk_data_end(wire);
            case phase::trailer_line: return decode_trailer_line(wire);
            case phase::message_end: break;
        }
        return {};
    }

    // Copies the body octets that both fit the staging budget and the
    // remaining count; the message ends exactly at the CL boundary.
    absorbed decode_length(std::string_view wire) {
        const std::size_t room = budget_.max_staged_bytes - staged_total_;
        if (room == 0) return {0, false, true};
        const std::size_t n = std::min(
            {wire.size(), room,
             static_cast<std::size_t>(length_remaining_)});
        if (n > 0) {
            stage(wire.substr(0, n));
            length_remaining_ -= n;
        }
        if (length_remaining_ == 0) {
            phase_ = phase::message_end;
            return {n, true};
        }
        return {n, false};
    }

    // Accumulates the bounded chunk-size line ("1*HEXDIG [ chunk-ext ]"
    // CRLF). The CRLF arrives split-safely: a lone CR parks until its
    // LF, whatever feeds later; an LF that does not close a CR is a
    // bare LF and malformed no matter what arrives next.
    absorbed decode_chunk_size(std::string_view wire) {
        std::size_t consumed = 0;
        while (!wire.empty()) {
            const char c = wire.front();
            wire.remove_prefix(1);
            ++consumed;
            if (saw_cr_) {
                saw_cr_ = false;
                if (c != '\n') {
                    return line_failed("bare CR in chunk-size line",
                                       consumed);
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

    // Parses the completed line and advances: a size of 0 (the
    // last-chunk, 1*"0") enters the trailer phase, anything else the
    // chunk-data phase with the exact remaining count.
    absorbed complete_chunk_size(std::size_t consumed) {
        static constexpr std::string_view kHexDigits =
            "0123456789abcdefABCDEF";
        // Own the bytes: line_ is reset below, and a view into it
        // would be clobbered by the clear()'s null terminator.
        const std::string line = std::move(line_);
        reset_line();
        const std::size_t ext_start = line.find_first_not_of(kHexDigits);
        const std::string_view hex =
            ext_start == std::string_view::npos
                ? std::string_view(line)
                : std::string_view(line).substr(0, ext_start);
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
        if (!parse_chunk_ext(ext_start == std::string_view::npos
                                 ? std::string_view()
                                 : std::string_view(line).substr(ext_start))) {
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

    // Copies the chunk data that fits the staging budget and the
    // chunk's remaining count, then demands the CRLF terminator.
    absorbed decode_chunk_data(std::string_view wire) {
        const std::size_t room = budget_.max_staged_bytes - staged_total_;
        if (room == 0) return {0, false, true};
        const std::size_t n = std::min(
            {wire.size(), room,
             static_cast<std::size_t>(length_remaining_)});
        if (n > 0) {
            stage(wire.substr(0, n));
            length_remaining_ -= n;
        }
        if (length_remaining_ == 0) phase_ = phase::chunk_data_end;
        return {n, false};
    }

    // Exactly CRLF after the chunk data; accumulated across feeds.
    absorbed decode_chunk_data_end(std::string_view wire) {
        std::size_t consumed = 0;
        while (!wire.empty()) {
            const char expected = end_seen_ == 0 ? '\r' : '\n';
            if (wire.front() != expected) {
                return line_failed(
                    "chunk data is not terminated by CRLF", consumed);
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

    // Accumulates one trailer field line, bounded by max_trailer_bytes.
    absorbed decode_trailer_line(std::string_view wire) {
        std::size_t consumed = 0;
        while (!wire.empty()) {
            const char c = wire.front();
            wire.remove_prefix(1);
            ++consumed;
            if (saw_cr_) {
                saw_cr_ = false;
                if (c != '\n') {
                    return line_failed("bare CR in trailer line",
                                       consumed);
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
                fail(http::outcome_code::limit_exceeded,
                     "trailer bytes budget exhausted");
                return {consumed, false};
            }
            line_.push_back(c);
        }
        return {consumed, false};
    }

    absorbed complete_trailer_line(std::size_t consumed) {
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

    // Validates one trailer field line and appends it; order and
    // repeats are preserved. Forbidden names are rejected per RFC 9112
    // section 7.1.3 (framing, routing, and security-sensitive fields
    // may not arrive as trailers).
    bool accept_trailer(std::string_view line) {
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            return fail(http::outcome_code::protocol_error,
                        "trailer line has no colon");
        }
        const std::string_view name = line.substr(0, colon);
        if (!http::detail::is_token(name)) {
            return fail(http::outcome_code::protocol_error,
                        "trailer field name is not a token");
        }
        const std::string_view value =
            detail_head::trim_ows(line.substr(colon + 1));
        if (detail_head::contains_field_value_ctl(value)) {
            return fail(http::outcome_code::protocol_error,
                        "CTL byte in trailer value");
        }
        if (trailers_.size() >= budget_.max_trailer_fields) {
            return fail(http::outcome_code::limit_exceeded,
                        "trailer fields budget exhausted");
        }
        if (is_forbidden_trailer(name)) {
            return fail(http::outcome_code::protocol_error,
                        "forbidden trailer field");
        }
        trailers_.append(name, value);
        return true;
    }

    bool is_forbidden_trailer(std::string_view name) const noexcept {
        static constexpr std::string_view kForbidden[] = {
            "content-length", "transfer-encoding", "host", "te",
            "connection", "expect", "upgrade", "authorization",
        };
        for (const std::string_view forbidden : kForbidden) {
            if (detail_head::ascii_iequals(name, forbidden)) return true;
        }
        return false;
    }

    // Grammar-exact chunk-ext (RFC 9112 section 7.1):
    //   chunk-ext = *( BWS ";" BWS chunk-ext-name
    //                    [ BWS "=" BWS chunk-ext-val ] )
    // Extensions are parsed and discarded. Returns false on any
    // malformed syntax.
    static bool parse_chunk_ext(std::string_view ext) {
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

    // The chunk-ext value after '=': a quoted-string (offset just past
    // its closing quote) or the unquoted run. The unquoted form accepts
    // tchar plus '=' (real-world spellings such as ext=a=1; pinned by
    // the framing matrix). npos on malformed or empty input.
    static std::size_t chunk_ext_val_span(std::string_view val) {
        if (val.empty()) return std::string_view::npos;
        if (val.front() != '"') return val_span(val);
        const std::size_t end = quoted_string_span(val.substr(1));
        return end == std::string_view::npos
                   ? std::string_view::npos : end + 1;
    }

    // The unquoted chunk-ext-val run: tchar plus '='.
    static std::size_t val_span(std::string_view v) noexcept {
        std::size_t n = 0;
        while (n < v.size() && (is_tchar(v[n]) || v[n] == '=')) ++n;
        return n;
    }

    // The body of a quoted-string value after the opening quote (RFC
    // 9110 section 5.6.4): qdtext and quoted-pair up to the closing
    // quote. Returns the offset just past the closing quote, npos on
    // malformed or unterminated input.
    static std::size_t quoted_string_span(std::string_view body) {
        for (std::size_t i = 0; i < body.size(); ++i) {
            const char c = body[i];
            if (c == '"') return i + 1;
            if (c == '\\') {
                if (i + 1 >= body.size()
                        || !is_quoted_pair_target(body[i + 1])) {
                    return std::string_view::npos;
                }
                ++i;
                continue;
            }
            if (!is_qdtext(c)) return std::string_view::npos;
        }
        return std::string_view::npos;  // unterminated
    }

    static bool is_tchar(const char c) noexcept {
        static constexpr std::string_view kTcharExtra = "!#$%&'*+-.^_`|~";
        return is_ascii_alnum(c)
            || kTcharExtra.find(c) != std::string_view::npos;
    }

    static constexpr bool is_ascii_alnum(const char c) noexcept {
        const auto u = static_cast<unsigned char>(c);
        return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
            || (u >= '0' && u <= '9');
    }

    // qdtext: HTAB / SP / %x21 / %x23-5B / %x5D-7E / obs-text.
    static constexpr bool is_qdtext(const char c) noexcept {
        const auto u = static_cast<unsigned char>(c);
        return c == '\t' || c == ' ' || u == 0x21
            || (u >= 0x23 && u <= 0x5B) || (u >= 0x5D && u <= 0x7E)
            || u >= 0x80;
    }

    // quoted-pair targets: HTAB / SP / VCHAR / obs-text.
    static constexpr bool is_quoted_pair_target(const char c) noexcept {
        const auto u = static_cast<unsigned char>(c);
        return c == '\t' || c == ' ' || (u >= 0x21 && u <= 0x7E)
            || u >= 0x80;
    }

    static std::string_view skip_bws(std::string_view v) noexcept {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.remove_prefix(1);
        }
        return v;
    }

    static std::size_t token_span(std::string_view v) noexcept {
        std::size_t n = 0;
        while (n < v.size() && is_tchar(v[n])) ++n;
        return n;
    }

    static unsigned char hex_value(const char c) noexcept {
        if (c >= '0' && c <= '9') {
            return static_cast<unsigned char>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<unsigned char>(c - 'a' + 10);
        }
        return static_cast<unsigned char>(c - 'A' + 10);
    }

    // Copies `chunk` into the staging queue; the caller has verified
    // it fits the budget.
    void stage(std::string_view chunk) {
        std::vector<std::byte> segment;
        segment.reserve(chunk.size());
        const auto* first = reinterpret_cast<const std::byte*>(
            chunk.data());
        segment.assign(first, first + chunk.size());
        staged_total_ += chunk.size();
        staging_.push_back(std::move(segment));
    }

    // Records a typed, sticky rejection. Returns false so callers can
    // write `return fail(...)`.
    bool fail(http::outcome_code code, std::string message) {
        failure_ = http::outcome(code,
                                 "http1_body_decoder: "
                                     + std::move(message));
        close_policy_ = code == http::outcome_code::limit_exceeded
                            ? http1_close_policy::respond_then_close
                            : http1_close_policy::close_now;
        failed_ = true;
        return false;
    }

    // Fails the decode after `consumed` benign bytes of the current
    // step (the offending byte stays with the caller).
    absorbed line_failed(const char* what, std::size_t consumed) {
        fail(http::outcome_code::protocol_error, what);
        return {consumed, false};
    }

    void reset_line() {
        line_.clear();
        saw_cr_ = false;
    }

    static constexpr std::size_t kMaxChunkSizeLineBytes = 1024;

    http1_body_budget budget_;
    http1_body_kind kind_ = http1_body_kind::none;
    http::outcome failure_;
    http1_close_policy close_policy_ = http1_close_policy::none;
    phase phase_ = phase::length;
    std::uint64_t length_remaining_ = 0;
    std::deque<std::vector<std::byte>> staging_;
    std::size_t staged_total_ = 0;
    http::fields trailers_;
    std::string line_;      // line under accumulation (size/trailer)
    bool saw_cr_ = false;   // the line parked on a lone CR
    // CRLF bytes seen in the chunk_data_end phase.
    unsigned end_seen_ = 0;
    bool failed_ = false;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_BODY_DECODER_HPP_
