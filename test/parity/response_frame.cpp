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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// See response_frame.hpp for the parser contract.

#include "response_frame.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace parity {
namespace {

constexpr std::size_t MAX_LINE_BYTES = 64u * 1024u;

std::size_t find_crlf(const std::string& s) {
    return s.find("\r\n");
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() && to_lower(a) == to_lower(b);
}

bool parse_decimal(const std::string& s, std::size_t& out) {
    if (s.empty()) return false;
    std::size_t value = 0;
    for (char ch : s) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        if (value > response_frame_parser::max_response_bytes) return false;
        value = value * 10 + static_cast<std::size_t>(ch - '0');
    }
    out = value;
    return true;
}

// RFC 7230 §3.1.2: status-line = HTTP-version SP status-code SP reason.
// A bare three-digit code with no trailing SP/reason is also accepted.
// Status-line check per the RFC 7230 §3.1.2 shape: protocol token SP
// 3-digit code [SP reason]. The protocol token is not restricted to
// "HTTP/n.n": SHOUTcast responses use "ICY 200 OK" (that is the entire
// point of the shoutcast transcript), while a non-numeric code or a
// missing code stays a parse error.
bool parse_status_line(const std::string& line, int& status_out) {
    std::size_t space = line.find(' ');
    if (space == std::string::npos || space == 0) return false;
    std::size_t code_begin = space + 1;
    if (code_begin + 3 > line.size()) return false;
    int code = 0;
    for (std::size_t i = code_begin; i < code_begin + 3; ++i) {
        char ch = line[i];
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        code = code * 10 + (ch - '0');
    }
    if (code_begin + 3 != line.size() && line[code_begin + 3] != ' ') return false;
    status_out = code;
    return true;
}

bool is_hex(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) ||
           (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return ch - 'A' + 10;
}

}  // namespace

std::vector<observed_response> response_frame_parser::feed(std::string_view bytes) {
    if (!error_) {
        if (bytes.size() > max_response_bytes) {
            fail("feed exceeds max_response_bytes");
        } else {
            buf_.append(bytes.data(), bytes.size());
            consume();
        }
    }
    return std::move(completed_);
}

std::vector<observed_response> response_frame_parser::finish() {
    if (!error_) {
        if (mode_ == mode::until_close) {
            finish_response();
            mode_ = mode::head;
        } else {
            check_truncation();
        }
    }
    return std::move(completed_);
}

const std::string& response_frame_parser::error() const {
    static const std::string empty;
    return error_ ? error_text_ : empty;
}

void response_frame_parser::fail(const std::string& message) {
    error_ = true;
    error_text_ = message;
    buf_.clear();
}

// EOF without a pending until-close response: only a partial response
// (or stray bytes) counts as truncation.
void response_frame_parser::check_truncation() {
    if (mode_ == mode::head) {
        for (char ch : buf_) {
            if (ch != '\r' && ch != '\n') {
                fail("connection closed with trailing bytes");
                return;
            }
        }
        buf_.clear();
        return;
    }
    fail("connection closed mid-response");
}

void response_frame_parser::finish_response() {
    observed_response r;
    r.raw_status_line = std::move(raw_status_line_);
    r.status = status_;
    r.headers = std::move(headers_);
    r.body = std::move(body_);
    r.framing = framing_;
    completed_.push_back(std::move(r));
    raw_status_line_.clear();
    status_ = 0;
    headers_.clear();
    body_.clear();
    framing_.clear();
    content_remaining_ = 0;
    chunk_remaining_ = 0;
}

void response_frame_parser::consume() {
    while (!error_) {
        switch (mode_) {
            case mode::head:
                if (!parse_head_line()) return;
                break;
            case mode::headers:
                if (!parse_header_line()) return;
                break;
            case mode::content_length:
                if (!parse_content_length()) return;
                break;
            case mode::chunked:
                if (!parse_chunk()) return;
                break;
            case mode::chunk_trailers:
                if (!parse_trailer_line()) return;
                break;
            case mode::until_close:
                body_.append(buf_);
                buf_.clear();
                return;
        }
    }
}

bool response_frame_parser::parse_head_line() {
    // RFC 7230 §3.5: tolerate leading CRLFs before the status line.
    while (buf_.compare(0, 2, "\r\n") == 0) buf_.erase(0, 2);
    if (find_crlf(buf_) == std::string::npos) {
        if (buf_.size() > MAX_LINE_BYTES) fail("status line too long");
        return false;
    }
    std::size_t crlf = find_crlf(buf_);
    std::string line = buf_.substr(0, crlf);
    buf_.erase(0, crlf + 2);
    if (!parse_status_line(line, status_)) {
        fail("malformed status line: " + line);
        return false;
    }
    raw_status_line_ = std::move(line);
    mode_ = mode::headers;
    return true;
}

bool response_frame_parser::parse_header_line() {
    if (find_crlf(buf_) == std::string::npos) {
        if (buf_.size() > MAX_LINE_BYTES) fail("header line too long");
        return false;
    }
    std::size_t crlf = find_crlf(buf_);
    std::string line = buf_.substr(0, crlf);
    buf_.erase(0, crlf + 2);
    if (line.empty()) {
        decide_framing();
        return true;
    }
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        fail("malformed header line: " + line);
        return false;
    }
    observed_header h;
    h.name = line.substr(0, colon);
    std::size_t value_begin = colon + 1;
    while (value_begin < line.size() &&
           (line[value_begin] == ' ' || line[value_begin] == '\t')) {
        ++value_begin;
    }
    h.value = line.substr(value_begin);
    headers_.push_back(std::move(h));
    return true;
}

void response_frame_parser::decide_framing() {
    bool chunked = false;
    bool has_length_header = false;
    std::size_t declared = 0;
    for (const observed_header& h : headers_) {
        if (iequals(h.name, "Transfer-Encoding") &&
            to_lower(h.value).find("chunked") != std::string::npos) {
            chunked = true;  // RFC 7230 §3.3.3: chunked wins over Content-Length
        } else if (iequals(h.name, "Content-Length")) {
            has_length_header = true;
            if (!parse_decimal(h.value, declared)) {
                fail("unparseable Content-Length: " + h.value);
                return;
            }
        }
    }
    if (status_ < 200 || status_ == 204 || status_ == 304) {
        // RFC 7230 §3.3.3: bodiless statuses complete immediately.
        framing_ = "none";
        finish_response();
        mode_ = mode::head;
        return;
    }
    if (head_only_) {
        // HEAD request: headers-only response regardless of declared
        // body length (RFC 7231 §4.3.2).
        framing_ = "none";
        finish_response();
        mode_ = mode::head;
        return;
    }
    if (chunked) {
        framing_ = "chunked";
        mode_ = mode::chunked;
        return;
    }
    if (has_length_header) {
        if (declared > max_response_bytes) {
            fail("declared body exceeds max_response_bytes");
            return;
        }
        framing_ = "content-length";
        content_remaining_ = declared;
        mode_ = mode::content_length;
        return;
    }
    framing_ = "none";
    mode_ = mode::until_close;
}

bool response_frame_parser::parse_content_length() {
    if (buf_.size() < content_remaining_) return false;
    body_.append(buf_, 0, content_remaining_);
    buf_.erase(0, content_remaining_);
    content_remaining_ = 0;
    mode_ = mode::head;
    finish_response();
    return true;
}

bool response_frame_parser::parse_chunk() {
    // The chunk size line is only consumed once its size is recorded;
    // chunk data may then span arbitrarily many feeds.
    if (chunk_remaining_ == 0) {
        std::size_t crlf = find_crlf(buf_);
        if (crlf == std::string::npos) {
            if (buf_.size() > MAX_LINE_BYTES) fail("chunk size line too long");
            return false;
        }
        std::string size_line = buf_.substr(0, crlf);
        std::size_t semi = size_line.find(';');
        std::string hex = size_line.substr(0, semi == std::string::npos ? size_line.size() : semi);
        if (hex.empty()) {
            fail("invalid chunk size: " + size_line);
            return false;
        }
        std::size_t chunk_size = 0;
        for (char ch : hex) {
            if (!is_hex(ch)) {
                fail("invalid chunk size: " + size_line);
                return false;
            }
            if (chunk_size > max_response_bytes) {
                fail("chunk size exceeds max_response_bytes");
                return false;
            }
            chunk_size = chunk_size * 16 + static_cast<std::size_t>(hex_value(ch));
        }
        buf_.erase(0, crlf + 2);
        if (chunk_size == 0) {
            mode_ = mode::chunk_trailers;
            return true;
        }
        chunk_remaining_ = chunk_size;
    }
    if (buf_.size() < chunk_remaining_ + 2) return false;
    if (buf_.compare(chunk_remaining_, 2, "\r\n") != 0) {
        fail("chunk data not terminated by CRLF");
        return false;
    }
    body_.append(buf_, 0, chunk_remaining_);
    buf_.erase(0, chunk_remaining_ + 2);
    chunk_remaining_ = 0;
    return true;
}

bool response_frame_parser::parse_trailer_line() {
    std::size_t crlf = find_crlf(buf_);
    if (crlf == std::string::npos) return false;
    std::string line = buf_.substr(0, crlf);
    buf_.erase(0, crlf + 2);
    if (line.empty()) {
        mode_ = mode::head;
        finish_response();
        return true;
    }
    // Trailer fields follow the same OWS rule as headers.
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        fail("malformed trailer line: " + line);
        return false;
    }
    std::string value = line.substr(colon + 1);
    std::size_t value_begin = value.find_first_not_of(" \t");
    if (value_begin == std::string::npos) value.clear();
    else value.erase(0, value_begin);
    headers_.push_back({line.substr(0, colon), std::move(value)});
    return true;
}

}  // namespace parity
