/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>

#include <httpserver/detail/http1_response_framer.hpp>

namespace httpserver::detail {

std::string_view http1_reason_phrase(std::uint16_t code) noexcept {
    struct phrase_entry {
        std::uint16_t code;
        std::string_view phrase;
    };
    static constexpr std::array<phrase_entry, 55> table = {{
        {100, "Continue"},
        {101, "Switching Protocols"},
        {103, "Early Hints"},
        {200, "OK"},
        {201, "Created"},
        {202, "Accepted"},
        {203, "Non-Authoritative Information"},
        {204, "No Content"},
        {205, "Reset Content"},
        {206, "Partial Content"},
        {300, "Multiple Choices"},
        {301, "Moved Permanently"},
        {302, "Found"},
        {303, "See Other"},
        {304, "Not Modified"},
        {305, "Use Proxy"},
        {307, "Temporary Redirect"},
        {308, "Permanent Redirect"},
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {402, "Payment Required"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {406, "Not Acceptable"},
        {407, "Proxy Authentication Required"},
        {408, "Request Timeout"},
        {409, "Conflict"},
        {410, "Gone"},
        {411, "Length Required"},
        {412, "Precondition Failed"},
        {413, "Content Too Large"},
        {414, "URI Too Long"},
        {415, "Unsupported Media Type"},
        {416, "Range Not Satisfiable"},
        {417, "Expectation Failed"},
        {421, "Misdirected Request"},
        {422, "Unprocessable Content"},
        {425, "Too Early"},
        {426, "Upgrade Required"},
        {428, "Precondition Required"},
        {429, "Too Many Requests"},
        {431, "Request Header Fields Too Large"},
        {451, "Unavailable For Legal Reasons"},
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
        {502, "Bad Gateway"},
        {503, "Service Unavailable"},
        {504, "Gateway Timeout"},
        {505, "HTTP Version Not Supported"},
        {506, "Variant Also Negotiates"},
        {507, "Insufficient Storage"},
        {508, "Loop Detected"},
        {510, "Not Extended"},
        {511, "Network Authentication Required"},
    }};
    for (const phrase_entry& entry : table) {
        if (entry.code == code) return entry.phrase;
    }
    return {};
}

void append_decimal(std::string& out, std::uint64_t value) {
    char digits[20];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count > 0) out.push_back(digits[--count]);
}

std::string format_imf_fixdate(std::chrono::system_clock::time_point when) {
    const std::time_t t = std::chrono::system_clock::to_time_t(when);
    std::tm tm_value{};
    gmtime_r(&t, &tm_value);
    char buf[32];
    const std::size_t n = std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S GMT", &tm_value);
    return n > 0 ? std::string(buf, n) : std::string();
}

auto http1_response_framer::start_head(std::string& out, const http::request_head& request, const http::status& s, const http::fields& response_fields) -> http::outcome {
    if (stage_ != stage::head) {
        return http::outcome(http::outcome_code::invalid_state, "http1_response_framer: head already started");
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

auto http1_response_framer::interim_head(std::string& out, std::uint16_t code) -> http::outcome {
    if (code < 100 || code > 199) {
        return http::outcome(http::outcome_code::invalid_argument, "http1_response_framer: interim status must be 1xx");
    }
    out.append("HTTP/1.1");
    out.push_back(' ');
    append_decimal(out, code);
    out.push_back(' ');
    const std::string_view phrase = http1_reason_phrase(code);
    if (!phrase.empty()) out.append(phrase);
    out.append("\r\n\r\n");
    return http::outcome::okay();
}

auto http1_response_framer::push_body(std::string& out, std::span<const std::byte> data, std::size_t max_out_bytes) -> http::outcome {
    if (stage_ == stage::failed) return failure_;
    if (stage_ == stage::done) {
        return http::outcome(http::outcome_code::invalid_state, "http1_response_framer: body already finished");
    }
    if (stage_ == stage::head) {
        return http::outcome(http::outcome_code::invalid_state, "http1_response_framer: head not started");
    }
    switch (mode_.kind) {
        case http1_response_body_kind::length: {
            const std::uint64_t remaining = mode_.content_length - body_written_;
            if (data.size() > remaining) {
                return fail_with(http::outcome(http::outcome_code::protocol_error,
                                               "http1_response_framer: body exceeds the declared"
                                               " Content-Length"));
            }
            const std::size_t n = std::min(data.size(), max_out_bytes);
            append_raw(out, data.first(n));
            body_written_ += n;
            return http::outcome::okay();
        }
        case http1_response_body_kind::chunked: {
            const std::size_t n = chunked_affordable(data.size(), max_out_bytes);
            if (n == 0) return http::outcome::okay();
            append_hex(out, n);
            out.append("\r\n");
            append_raw(out, data.first(n));
            out.append("\r\n");
            return http::outcome::okay();
        }
        case http1_response_body_kind::close_delimited: {
            // The body runs to EOF: raw bytes, no per-chunk framing.
            // The connection close after the drain is the response's
            // terminator (the keep-alive verdict already says close).
            const std::size_t n = std::min(data.size(), max_out_bytes);
            append_raw(out, data.first(n));
            return http::outcome::okay();
        }
        default:
            return fail_with(http::outcome(http::outcome_code::protocol_error,
                                           "http1_response_framer: response kind declares no"
                                           " body"));
    }
}

auto http1_response_framer::finish_body(std::string& out, const http::fields& trailers) -> http::outcome {
    if (stage_ == stage::failed) return failure_;
    if (stage_ == stage::done) {
        return http::outcome(http::outcome_code::invalid_state, "http1_response_framer: body already finished");
    }
    if (stage_ == stage::head) {
        return http::outcome(http::outcome_code::invalid_state, "http1_response_framer: head not started");
    }
    const http::outcome pre = validate_finish(trailers);
    if (!pre.ok()) return fail_with(pre);
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

auto http1_response_framer::chunked_affordable(std::size_t available, std::size_t max_out_bytes) -> std::size_t {
    if (max_out_bytes < 6 || available == 0) return 0;
    std::size_t candidate = std::min(available, max_out_bytes - 4);
    while (hex_width(candidate) + 4 + candidate > max_out_bytes) {
        --candidate;
    }
    return candidate;
}

auto http1_response_framer::append_hex(std::string& out, std::size_t value) -> void {
    char digits[16];
    std::size_t count = 0;
    do {
        digits[count++] = hex_digit(value % 16);
        value /= 16;
    } while (value != 0);
    while (count > 0) out.push_back(digits[--count]);
}

auto http1_response_framer::hex_width(std::size_t value) noexcept -> std::size_t {
    std::size_t width = 1;
    while (value >= 16) {
        value /= 16;
        ++width;
    }
    return width;
}

auto http1_response_framer::fail_with(const http::outcome& failure) -> http::outcome {
    failure_ = failure;
    stage_ = stage::failed;
    return failure;
}

auto http1_response_framer::validate_fields(const http::fields& fields) -> http::outcome {
    for (const http::fields::entry& e : fields.entries()) {
        if (!http::detail::is_token(e.name)) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "http1_response_framer: response field name is not"
                                 " an RFC 9110 token");
        }
        if (detail_head::contains_field_value_ctl(e.value)) {
            return http::outcome(http::outcome_code::invalid_argument,
                                 "http1_response_framer: CTL byte in response field"
                                 " value");
        }
    }
    return http::outcome::okay();
}

auto http1_response_framer::validate_finish(const http::fields& trailers) const -> http::outcome {
    if (mode_.kind == http1_response_body_kind::length && body_written_ != mode_.content_length) {
        return http::outcome(http::outcome_code::protocol_error,
                             "http1_response_framer: body shorter than the"
                             " declared Content-Length");
    }
    if (!trailers.empty() && mode_.kind != http1_response_body_kind::chunked) {
        return http::outcome(http::outcome_code::protocol_error,
                             "http1_response_framer: trailers on a non-chunked"
                             " response");
    }
    if (!trailers.empty()) return validate_fields(trailers);
    return http::outcome::okay();
}

auto http1_response_framer::append_status_line(std::string& out, const http::request_head& request, const http::status& s) const -> void {
    if (s.is_shoutcast()) {
        out.append("ICY");
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

auto http1_response_framer::append_handler_fields(std::string& out, const http::fields& fields, http1_response_body_kind kind) -> void {
    for (const http::fields::entry& e : fields.entries()) {
        if (detail_head::ascii_iequals(e.name, "connection")) continue;
        if (kind == http1_response_body_kind::none && (detail_head::ascii_iequals(e.name, "content-length") || detail_head::ascii_iequals(e.name, "transfer-encoding"))) {
            continue;
        }
        if (kind == http1_response_body_kind::metadata_only && detail_head::ascii_iequals(e.name, "transfer-encoding")) {
            continue;
        }
        if (kind == http1_response_body_kind::length && detail_head::ascii_iequals(e.name, "content-length")) {
            continue;
        }
        out.append(e.name);
        out.append(": ");
        out.append(e.value);
        out.append("\r\n");
    }
}

auto http1_response_framer::append_engine_fields(std::string& out, const http::request_head& request, const http::fields& fields) const -> void {
    const http1_keepalive keep = http1_response_keepalive(request, mode_.kind, mode_.close_policy);
    if (fields.first("date") == std::nullopt && clock_.now != nullptr) {
        out.append("Date: ");
        out.append(format_imf_fixdate(clock_.now()));
        out.append("\r\n");
    }
    if (keep == http1_keepalive::close) {
        out.append("Connection: close\r\n");
    } else if (request.request_protocol == http::protocol::http_1_0) {
        out.append("Connection: keep-alive\r\n");
    }
    if (mode_.kind == http1_response_body_kind::length) {
        out.append("Content-Length: ");
        append_decimal(out, mode_.content_length);
        out.append("\r\n");
    } else if (mode_.kind == http1_response_body_kind::chunked && fields.first("transfer-encoding") == std::nullopt) {
        out.append("Transfer-Encoding: chunked\r\n");
    }
}

}  // namespace httpserver::detail
