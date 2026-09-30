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
    static constexpr std::array<phrase_entry, 58> table = {{
        {100, "Continue"},                     {101, "Switching Protocols"},
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
        append_handler_fields(out, response_fields);
        // The head terminator: the engine fields (added in a later
        // step) serialize before it.
        out.append("\r\n");
        stage_ = stage::body;
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

    static void append_handler_fields(std::string& out,
                                      const http::fields& fields) {
        for (const http::fields::entry& e : fields.entries()) {
            out.append(e.name);
            out.append(": ");
            out.append(e.value);
            out.append("\r\n");
        }
    }

    clock_source clock_;
    std::string status_token_;
    http1_response_mode mode_;
    http::outcome failure_;
    stage stage_ = stage::head;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_RESPONSE_FRAMER_HPP_
