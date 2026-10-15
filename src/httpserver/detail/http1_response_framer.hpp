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

// TASK-107: HTTP/1 response wire serializer (PRD-V3N-REQ-004/026,
// DR-V3-006). NOT part of the installed surface.
//
// One framer per response, stateful, pure byte production: no I/O, no
// locks, no allocation policy — every method appends bytes to the
// caller's buffer and returns a typed outcome. The framer consumes the
// http1_response_mode decision (computed exactly once by its caller at
// start_head time) and never recomputes it. Strictness deltas vs the
// v2 emission are pre-approved for v3 and
// recorded as migration notes at their sites.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_response_framer.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_FRAMER_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_FRAMER_HPP_

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/detail/http1_head_parser.hpp>
#include <httpserver/detail/http1_response_mode.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

namespace detail {

// RFC 9110 section 15 reason phrases, indexed by linear scan (the table
// is tiny and cache-resident). Empty for valid codes the RFC leaves
// unassigned: the wire carries just the code.
std::string_view http1_reason_phrase(std::uint16_t code) noexcept;

// Appends @p value as a decimal digit string (no zero padding).
void append_decimal(std::string& out, std::uint64_t value);

// IMF-fixdate (RFC 9110 section 5.6.7.1), the one HTTP/1.1 Date form:
// "Sun, 06 Nov 1994 08:49:37 GMT". C-locale %a/%b spellings are the
// IMF ones by construction; the tests pin the format.
std::string format_imf_fixdate(
    std::chrono::system_clock::time_point when);

class http1_response_framer {
 public:
    // Engine seam for the emission clock. An empty now() omits the Date
    // engine field entirely; tests inject fixed values.
    struct clock_source {
        std::function<std::chrono::system_clock::time_point()> now;
    };

    explicit http1_response_framer(clock_source clock = {})
        : clock_(std::move(clock)) { }

    http1_response_framer(const http1_response_framer&) = delete;
    http1_response_framer& operator=(const http1_response_framer&) = delete;

    // Serializes the response head into @p out: status line (version
    // mirrors the request protocol unless status carries SHOUTcast metadata),
    // then the handler fields in entries() order, then the engine
    // fields. A typed failure (rejected mode, or a handler field that
    // is not wire-safe) appends nothing and leaves the framer failed —
    // the caller must close instead of emitting.
    http::outcome start_head(std::string& out,
                             const http::request_head& request,
                             const http::status& s,
                             const http::fields& response_fields);

    // Interim response head ("HTTP/1.1 100 Continue CRLF CRLF" et
    // cetera), appended ahead of the final head in the same buffer.
    // Informational responses travel this path only; the version token
    // is always HTTP/1.1, independent of final-response metadata.
    http::outcome interim_head(std::string& out, std::uint16_t code);

    // Frames one body chunk into @p out, bounded by @p max_out_bytes.
    // length: raw bytes; a chunk may split at the output bound (the
    // caller re-pushes the rest), but a push that would exceed the
    // declared total is a sticky protocol error that copies nothing.
    // chunked: the push encodes exactly one chunk of the affordable
    // prefix (lowercase hex size, CRLF, bytes, CRLF); when not even one
    // payload byte fits, nothing is appended and ok is returned — the
    // caller reads the appended delta and re-pushes. The no-body kinds
    // reject body bytes strictly.
    http::outcome push_body(std::string& out,
                            std::span<const std::byte> data,
                            std::size_t max_out_bytes);

    // Ends the body. chunked encodes the "0 CRLF" terminator, the
    // trailers in entries() order, and the final CRLF; every other kind
    // rejects trailers (they have no framing to ride) and appends
    // nothing. A length body that has not seen its declared total is a
    // sticky protocol error.
    http::outcome finish_body(std::string& out,
                              const http::fields& trailers);

    // The computed framing decision (valid after start_head).
    const http1_response_mode& mode() const noexcept { return mode_; }

    // True after a typed failure; the framer emits nothing further.
    bool failed() const noexcept { return stage_ == stage::failed; }

 private:
    // Framer lifecycle. head: start_head pending; body: head emitted,
    // body framing runs; done: the body ended; failed: a typed failure
    // is sticky.
    enum class stage : std::uint8_t { head, body, done, failed };

    // Largest payload size whose one-chunk encoding (hex size, CRLF,
    // bytes, CRLF) fits in @p max_out_bytes; 0 when not even one byte
    // fits.
    static std::size_t chunked_affordable(std::size_t available,
                                          std::size_t max_out_bytes);

    static void append_raw(std::string& out, std::span<const std::byte> data) {
        out.append(reinterpret_cast<const char*>(data.data()), data.size());
    }

    static constexpr char hex_digit(std::size_t value) noexcept {
        return value < 10 ? static_cast<char>('0' + value)
                          : static_cast<char>('a' + value - 10);
    }

    static void append_hex(std::string& out, std::size_t value);

    static std::size_t hex_width(std::size_t value) noexcept;

    // Records @p failure as sticky and returns it.
    http::outcome fail_with(const http::outcome& failure);

    // Handler fields must be wire-safe: token names, CTL-free values
    // (HTAB is OWS, not a CTL). Migration note: v2 (MHD) checked
    // nothing here and could emit a response-smuggling head from a bad
    // handler field; v3 fails the response typed instead.
    static http::outcome validate_fields(const http::fields& fields);

    // The pre-encode finish validation shared by every kind, in order:
    // a length body that missed its declared total; trailers on a kind
    // with no trailer framing (strictness delta vs possible v2
    // tolerance: without chunked they have no wire form, so v3 fails
    // the response typed instead of dropping them); and the head's
    // wire-safe rule applied to the trailer occurrences unchanged —
    // token names, CTL-free values (plan section 4 "same validation").
    // All checks precede the first appended byte.
    http::outcome validate_finish(const http::fields& trailers) const;

    // status-line = token SP code SP [reason] CRLF; the SP before an
    // empty phrase is kept (the ABNF makes it mandatory, the phrase
    // optional).
    void append_status_line(std::string& out,
                            const http::request_head& request,
                            const http::status& s) const;

    // Handler field occurrences in entries() order, minus the names the
    // mode strips from the EMISSION only (the committed fields object
    // is never mutated):
    //   - "Connection" is always stripped: the engine owns the header
    //     (documented v3 policy delta; the keep-alive verdict is
    //     re-emitted as an engine field below).
    //   - kind none (1xx/204): Content-Length and Transfer-Encoding are
    //     stripped (RFC 9110 section 8.6/11.6 MUST NOT).
    //   - kind metadata_only (304): Transfer-Encoding is stripped; a
    //     Content-Length rides as metadata.
    //   - kind head_no_body (HEAD): everything passes through verbatim.
    //   - kind length: the handler Content-Length is stripped here and
    //     re-emitted canonically as the engine framing field (same
    //     validated value, fixed position).
    static void append_handler_fields(std::string& out,
                                      const http::fields& fields,
                                      http1_response_body_kind kind);

    // The engine fields in fixed order: Date (only with a configured
    // clock and no handler Date), Connection (only when the verdict or
    // an HTTP/1.0 keep-alive requires the header), then exactly one
    // framing field (the canonical Content-Length, or
    // "Transfer-Encoding: chunked" when the engine selected chunked —
    // a handler-pinned chunked occurrence is the framing field and
    // passes through above).
    void append_engine_fields(std::string& out,
                              const http::request_head& request,
                              const http::fields& fields) const;

    clock_source clock_;
    http1_response_mode mode_;
    http::outcome failure_;
    std::uint64_t body_written_ = 0;  // length framing: raw bytes so far
    stage stage_ = stage::head;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_FRAMER_HPP_
