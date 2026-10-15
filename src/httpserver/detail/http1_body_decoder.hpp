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
#include <httpserver/detail/resource_charge.hpp>

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
        const server::budget_limits& limits) noexcept;
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
    http1_body_decoder(const http1_body_mode& mode, http1_body_budget budget, server::resource_budget scope = {});

    // Frames a prefix of `wire` into the staging queue and returns how
    // much was consumed. Idempotent after complete/failed: complete
    // reports {complete, 0}, failure reports {failed, 0}.
    http1_body_progress decode(std::string_view wire);

    // THE consumption event: copies staged bytes into `into` and
    // releases staging room for exactly the bytes copied. Verdict
    // order: failed, then data (an empty destination never receives a
    // zero-byte data pull), then end (message end with the queue
    // drained), then empty.
    detail::body_pull_result pull(std::span<std::byte> into);

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
    absorbed absorb_loop(std::string_view wire);

    absorbed absorb_phase(std::string_view wire);

    // Copies the body octets that both fit the staging budget and the
    // remaining count; the message ends exactly at the CL boundary.
    absorbed decode_length(std::string_view wire);

    // Accumulates the bounded chunk-size line ("1*HEXDIG [ chunk-ext ]"
    // CRLF). The CRLF arrives split-safely: a lone CR parks until its
    // LF, whatever feeds later; an LF that does not close a CR is a
    // bare LF and malformed no matter what arrives next.
    absorbed decode_chunk_size(std::string_view wire);

    // Parses the completed line and advances: a size of 0 (the
    // last-chunk, 1*"0") enters the trailer phase, anything else the
    // chunk-data phase with the exact remaining count.
    absorbed complete_chunk_size(std::size_t consumed);

    // Copies the chunk data that fits the staging budget and the
    // chunk's remaining count, then demands the CRLF terminator.
    absorbed decode_chunk_data(std::string_view wire);

    // Exactly CRLF after the chunk data; accumulated across feeds.
    absorbed decode_chunk_data_end(std::string_view wire);

    // Accumulates one trailer field line, bounded by max_trailer_bytes.
    absorbed decode_trailer_line(std::string_view wire);

    absorbed complete_trailer_line(std::size_t consumed);

    // Validates one trailer field line and appends it; order and
    // repeats are preserved. Forbidden names are rejected per RFC 9112
    // section 7.1.3 (framing, routing, and security-sensitive fields
    // may not arrive as trailers).
    bool accept_trailer(std::string_view line);

    bool is_forbidden_trailer(std::string_view name) const noexcept;

    // Grammar-exact chunk-ext (RFC 9112 section 7.1):
    //   chunk-ext = *( BWS ";" BWS chunk-ext-name
    //                    [ BWS "=" BWS chunk-ext-val ] )
    // Extensions are parsed and discarded. Returns false on any
    // malformed syntax.
    static bool parse_chunk_ext(std::string_view ext);

    // The chunk-ext value after '=': a quoted-string (offset just past
    // its closing quote) or the unquoted run. The unquoted form accepts
    // tchar plus '=' (real-world spellings such as ext=a=1; pinned by
    // the framing matrix). npos on malformed or empty input.
    static std::size_t chunk_ext_val_span(std::string_view val);

    // The unquoted chunk-ext-val run: tchar plus '='.
    static std::size_t val_span(std::string_view v) noexcept;

    // The body of a quoted-string value after the opening quote (RFC
    // 9110 section 5.6.4): qdtext and quoted-pair up to the closing
    // quote. Returns the offset just past the closing quote, npos on
    // malformed or unterminated input.
    static std::size_t quoted_string_span(std::string_view body);

    static bool is_tchar(const char c) noexcept;

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

    static std::string_view skip_bws(std::string_view v) noexcept;

    static std::size_t token_span(std::string_view v) noexcept;

    static unsigned char hex_value(const char c) noexcept;

    // Copies `chunk` into the staging queue; the caller has verified
    // it fits the budget.
    bool stage(std::string_view chunk);

    // Records a typed, sticky rejection. Returns false so callers can
    // write `return fail(...)`.
    bool fail(http::outcome_code code, std::string message);

    // Fails the decode after `consumed` benign bytes of the current
    // step (the offending byte stays with the caller).
    absorbed line_failed(const char* what, std::size_t consumed);

    void reset_line();

    static constexpr std::size_t kMaxChunkSizeLineBytes = 1024;

    http1_body_budget budget_;
    resource_charge storage_charge_, trailer_bytes_, trailer_fields_;
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
