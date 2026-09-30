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
// v2 (libmicrohttpd-backed) emission are pre-approved for v3 and
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
inline std::string_view http1_reason_phrase(std::uint16_t code) noexcept {
    struct phrase_entry {
        std::uint16_t code;
        std::string_view phrase;
    };
    static constexpr std::array<phrase_entry, 59> table = {{
        {100, "Continue"},                     {101, "Switching Protocols"},
        {103, "Early Hints"},                  {200, "OK"},
        {200, "OK"},                           {201, "Created"},
        {202, "Accepted"},                     {203, "Non-Authoritative Information"},
        {204, "No Content"},                   {205, "Reset Content"},
        {206, "Partial Content"},              {300, "Multiple Choices"},
        {301, "Moved Permanently"},            {302, "Found"},
        {303, "See Other"},                    {304, "Not Modified"},
        {305, "Use Proxy"},                    {307, "Temporary Redirect"},
        {308, "Permanent Redirect"},           {400, "Bad Request"},
        {401, "Unauthorized"},                 {402, "Payment Required"},
        {403, "Forbidden"},                    {404, "Not Found"},
        {405, "Method Not Allowed"},           {406, "Not Acceptable"},
        {407, "Proxy Authentication Required"}, {408, "Request Timeout"},
        {409, "Conflict"},                     {410, "Gone"},
        {411, "Length Required"},              {412, "Precondition Failed"},
        {413, "Content Too Large"},            {414, "URI Too Long"},
        {415, "Unsupported Media Type"},       {416, "Range Not Satisfiable"},
        {417, "Expectation Failed"},           {421, "Misdirected Request"},
        {422, "Unprocessable Content"},        {425, "Too Early"},
        {426, "Upgrade Required"},             {428, "Precondition Required"},
        {429, "Too Many Requests"},            {431, "Request Header Fields Too Large"},
        {451, "Unavailable For Legal Reasons"}, {500, "Internal Server Error"},
        {501, "Not Implemented"},              {502, "Bad Gateway"},
        {503, "Service Unavailable"},          {504, "Gateway Timeout"},
        {505, "HTTP Version Not Supported"},   {506, "Variant Also Negotiates"},
        {507, "Insufficient Storage"},         {508, "Loop Detected"},
        {510, "Not Extended"},                 {511, "Network Authentication Required"},
    }};
    for (const phrase_entry& entry : table) {
        if (entry.code == code) return entry.phrase;
    }
    return {};
}

// Appends @p value as a decimal digit string (no zero padding).
inline void append_decimal(std::string& out, std::uint64_t value) {
    char digits[20];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count > 0) out.push_back(digits[--count]);
}

// IMF-fixdate (RFC 9110 section 5.6.7.1), the one HTTP/1.1 Date form:
// "Sun, 06 Nov 1994 08:49:37 GMT". C-locale %a/%b spellings are the
// IMF ones by construction; the tests pin the format.
inline std::string format_imf_fixdate(
    std::chrono::system_clock::time_point when) {
    const std::time_t t = std::chrono::system_clock::to_time_t(when);
    std::tm tm_value{};
    gmtime_r(&t, &tm_value);
    char buf[32];
    const std::size_t n = std::strftime(buf, sizeof buf,
                                        "%a, %d %b %Y %H:%M:%S GMT",
                                        &tm_value);
    return n > 0 ? std::string(buf, n) : std::string();
}

class http1_response_framer {
 public:
    // Engine seam for the emission clock. An empty now() omits the Date
    // engine field entirely; tests inject fixed values.
    struct clock_source {
        std::function<std::chrono::system_clock::time_point()> now;
    };

    // @p status_token replaces the HTTP-version token of the status
    // line (the SHOUTcast "ICY" wire form is the one corpus pin).
    explicit http1_response_framer(clock_source clock = {},
                                   std::string_view status_token = {})
        : clock_(std::move(clock)), status_token_(status_token) { }

    http1_response_framer(const http1_response_framer&) = delete;
    http1_response_framer& operator=(const http1_response_framer&) = delete;

    // Serializes the response head into @p out: status line (version
    // mirrors the request protocol unless a status token was given),
    // then the handler fields in entries() order, then the engine
    // fields. A typed failure (rejected mode, or a handler field that
    // is not wire-safe) appends nothing and leaves the framer failed —
    // the caller must close instead of emitting.
    http::outcome start_head(std::string& out,
                             const http::request_head& request,
                             const http::status& s,
                             const http::fields& response_fields) {
        if (stage_ != stage::head) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "http1_response_framer: head already started");
        }
        mode_ = http1_response_mode::compute(request, s, response_fields);
        if (!mode_.failure.ok()) return fail_with(mode_.failure);
        // Every handler field is validated BEFORE the status line is
        // appended: a bad field must never leave a half-written head in
        // the caller's buffer.
        const http::outcome fields_ok = validate_fields(response_fields);
        if (!fields_ok.ok()) return fail_with(fields_ok);
        append_status_line(out, request, s);
        append_handler_fields(out, response_fields, mode_.kind);
        append_engine_fields(out, request, response_fields);
        // The head terminator: the engine fields serialize before it.
        out.append("\r\n");
        stage_ = stage::body;
        return http::outcome::okay();
    }

    // Interim response head ("HTTP/1.1 100 Continue CRLF CRLF" et
    // cetera), appended ahead of the final head in the same buffer.
    // Informational responses travel this path only; the version token
    // is HTTP/1.1 (or the configured status token).
    http::outcome interim_head(std::string& out, std::uint16_t code) {
        if (code < 100 || code > 199) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "http1_response_framer: interim status must be 1xx");
        }
        if (!status_token_.empty()) {
            out.append(status_token_);
        } else {
            out.append("HTTP/1.1");
        }
        out.push_back(' ');
        append_decimal(out, code);
        out.push_back(' ');
        const std::string_view phrase = http1_reason_phrase(code);
        if (!phrase.empty()) out.append(phrase);
        out.append("\r\n\r\n");
        return http::outcome::okay();
    }

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
                            std::size_t max_out_bytes) {
        if (stage_ == stage::failed) return failure_;
        if (stage_ == stage::done) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "http1_response_framer: body already finished");
        }
        if (stage_ == stage::head) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "http1_response_framer: head not started");
        }
        switch (mode_.kind) {
            case http1_response_body_kind::length: {
                const std::uint64_t remaining =
                    mode_.content_length - body_written_;
                if (data.size() > remaining) {
                    return fail_with(http::outcome(
                        http::outcome_code::protocol_error,
                        "http1_response_framer: body exceeds the declared"
                        " Content-Length"));
                }
                const std::size_t n = std::min(data.size(), max_out_bytes);
                append_raw(out, data.first(n));
                body_written_ += n;
                return http::outcome::okay();
            }
            case http1_response_body_kind::chunked: {
                const std::size_t n =
                    chunked_affordable(data.size(), max_out_bytes);
                if (n == 0) return http::outcome::okay();
                append_hex(out, n);
                out.append("\r\n");
                append_raw(out, data.first(n));
                out.append("\r\n");
                return http::outcome::okay();
            }
            default:
                return fail_with(http::outcome(
                    http::outcome_code::protocol_error,
                    "http1_response_framer: response kind declares no"
                    " body"));
        }
    }

    // Ends the body. chunked encodes the "0 CRLF" terminator, the
    // trailers in entries() order, and the final CRLF; every other kind
    // rejects trailers (they have no framing to ride) and appends
    // nothing. A length body that has not seen its declared total is a
    // sticky protocol error.
    http::outcome finish_body(std::string& out,
                              const http::fields& trailers) {
        if (stage_ == stage::failed) return failure_;
        if (stage_ == stage::done) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "http1_response_framer: body already finished");
        }
        if (stage_ == stage::head) {
            return http::outcome(
                http::outcome_code::invalid_state,
                "http1_response_framer: head not started");
        }
        if (mode_.kind == http1_response_body_kind::length) {
            if (body_written_ != mode_.content_length) {
                return fail_with(http::outcome(
                    http::outcome_code::protocol_error,
                    "http1_response_framer: body shorter than the"
                    " declared Content-Length"));
            }
        }
        if (!trailers.empty()
                && mode_.kind != http1_response_body_kind::chunked) {
            // Strictness delta vs possible v2 tolerance: trailers
            // without chunked framing have no wire form, so v3 fails
            // the response typed instead of dropping them.
            return fail_with(http::outcome(
                http::outcome_code::protocol_error,
                "http1_response_framer: trailers on a non-chunked"
                " response"));
        }
        if (mode_.kind == http1_response_body_kind::chunked) {
            out.append("0\r\n");
            for (const http::fields::entry& e : trailers.entries()) {
                out.append(e.name);
                out.append(": ");
                out.append(e.value);
                out.append("\r\n");
            }
            out.append("\r\n");
        }
        stage_ = stage::done;
        return http::outcome::okay();
    }

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
                                          std::size_t max_out_bytes) {
        if (max_out_bytes < 6 || available == 0) return 0;
        std::size_t candidate = std::min(available, max_out_bytes - 4);
        while (hex_width(candidate) + 4 + candidate > max_out_bytes) {
            --candidate;
        }
        return candidate;
    }

    static void append_raw(std::string& out, std::span<const std::byte> data) {
        out.append(reinterpret_cast<const char*>(data.data()), data.size());
    }

    static constexpr char hex_digit(std::size_t value) noexcept {
        return value < 10 ? static_cast<char>('0' + value)
                          : static_cast<char>('a' + value - 10);
    }

    static void append_hex(std::string& out, std::size_t value) {
        char digits[16];
        std::size_t count = 0;
        do {
            digits[count++] = hex_digit(value % 16);
            value /= 16;
        } while (value != 0);
        while (count > 0) out.push_back(digits[--count]);
    }

    static std::size_t hex_width(std::size_t value) noexcept {
        std::size_t width = 1;
        while (value >= 16) {
            value /= 16;
            ++width;
        }
        return width;
    }

    // Records @p failure as sticky and returns it.
    http::outcome fail_with(const http::outcome& failure) {
        failure_ = failure;
        stage_ = stage::failed;
        return failure;
    }

    // Handler fields must be wire-safe: token names, CTL-free values
    // (HTAB is OWS, not a CTL). Migration note: v2 (MHD) checked
    // nothing here and could emit a response-smuggling head from a bad
    // handler field; v3 fails the response typed instead.
    static http::outcome validate_fields(const http::fields& fields) {
        for (const http::fields::entry& e : fields.entries()) {
            if (!http::detail::is_token(e.name)) {
                return http::outcome(
                    http::outcome_code::invalid_argument,
                    "http1_response_framer: response field name is not"
                    " an RFC 9110 token");
            }
            if (detail_head::contains_field_value_ctl(e.value)) {
                return http::outcome(
                    http::outcome_code::invalid_argument,
                    "http1_response_framer: CTL byte in response field"
                    " value");
            }
        }
        return http::outcome::okay();
    }

    // status-line = token SP code SP [reason] CRLF; the SP before an
    // empty phrase is kept (the ABNF makes it mandatory, the phrase
    // optional).
    void append_status_line(std::string& out,
                            const http::request_head& request,
                            const http::status& s) const {
        if (!status_token_.empty()) {
            out.append(status_token_);
        } else {
            out.append(http::to_string(request.request_protocol));
        }
        out.push_back(' ');
        append_decimal(out, s.code());
        out.push_back(' ');
        const std::string_view phrase = http1_reason_phrase(s.code());
        if (!phrase.empty()) out.append(phrase);
        out.append("\r\n");
    }

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
                                      http1_response_body_kind kind) {
        for (const http::fields::entry& e : fields.entries()) {
            if (detail_head::ascii_iequals(e.name, "connection")) continue;
            if (kind == http1_response_body_kind::none
                && (detail_head::ascii_iequals(e.name, "content-length")
                    || detail_head::ascii_iequals(
                        e.name, "transfer-encoding"))) {
                continue;
            }
            if (kind == http1_response_body_kind::metadata_only
                && detail_head::ascii_iequals(e.name,
                                              "transfer-encoding")) {
                continue;
            }
            if (kind == http1_response_body_kind::length
                && detail_head::ascii_iequals(e.name, "content-length")) {
                continue;
            }
            out.append(e.name);
            out.append(": ");
            out.append(e.value);
            out.append("\r\n");
        }
    }

    // The engine fields in fixed order: Date (only with a configured
    // clock and no handler Date), Connection (only when the verdict or
    // an HTTP/1.0 keep-alive requires the header), then exactly one
    // framing field (the canonical Content-Length, or
    // "Transfer-Encoding: chunked" when the engine selected chunked —
    // a handler-pinned chunked occurrence is the framing field and
    // passes through above).
    void append_engine_fields(std::string& out,
                              const http::request_head& request,
                              const http::fields& fields) const {
        const http1_keepalive keep = http1_response_keepalive(
            request, mode_.kind, mode_.close_policy);
        if (fields.first("date") == std::nullopt && clock_.now != nullptr) {
            out.append("Date: ");
            out.append(format_imf_fixdate(clock_.now()));
            out.append("\r\n");
        }
        if (keep == http1_keepalive::close) {
            out.append("Connection: close\r\n");
        } else if (request.request_protocol
                       == http::protocol::http_1_0) {
            out.append("Connection: keep-alive\r\n");
        }
        if (mode_.kind == http1_response_body_kind::length) {
            out.append("Content-Length: ");
            append_decimal(out, mode_.content_length);
            out.append("\r\n");
        } else if (mode_.kind == http1_response_body_kind::chunked
                   && fields.first("transfer-encoding")
                          == std::nullopt) {
            out.append("Transfer-Encoding: chunked\r\n");
        }
    }

    clock_source clock_;
    std::string status_token_;
    http1_response_mode mode_;
    http::outcome failure_;
    std::uint64_t body_written_ = 0;  // length framing: raw bytes so far
    stage stage_ = stage::head;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_FRAMER_HPP_
