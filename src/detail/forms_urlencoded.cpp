/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/forms_urlencoded.hpp>
#include <string>
#include <utility>

namespace httpserver::detail {

auto urlencoded_decoder::feed(std::span<const std::byte> wire) -> http::outcome {
    if (failed_) return failure_;
    for (const std::byte b : wire) {
        if (bytes_seen_ >= max_total_bytes_) {
            fail(http::outcome_code::limit_exceeded, "raw body bytes budget exhausted");
            return failure_;
        }
        ++bytes_seen_;
        if (!feed_byte(static_cast<unsigned char>(b))) {
            return failure_;
        }
    }
    return http::outcome::okay();
}

auto urlencoded_decoder::finish() -> http::outcome {
    if (failed_) return failure_;
    if (escaping_) {
        fail(http::outcome_code::invalid_argument, "%HH escape truncated at end of body");
        return failure_;
    }
    if (saw_token_byte_ && !commit_field()) return failure_;
    return http::outcome::okay();
}

auto urlencoded_decoder::feed_byte(unsigned char c) -> bool {
    if (escaping_) return feed_escape(c);
    if (c == '&') return flush_token();
    saw_token_byte_ = true;
    switch (c) {
        case '+':
            append(' ');
            return true;
        case '%':
            escaping_ = true;
            escape_digits_ = 0;
            return true;
        case '=':
            if (in_value_) {
                value_.push_back('=');
            } else {
                in_value_ = true;
            }
            return true;
        default:
            append(static_cast<char>(c));
            return true;
    }
}

auto urlencoded_decoder::feed_escape(unsigned char c) -> bool {
    if (!is_hex(c)) {
        return fail(http::outcome_code::invalid_argument, "malformed %HH escape");
    }
    escape_value_ = escape_value_ * 16 + hex_value(c);
    if (++escape_digits_ == 2) {
        escaping_ = false;
        append(static_cast<char>(escape_value_));
    }
    return true;
}

auto urlencoded_decoder::flush_token() -> bool {
    if (!saw_token_byte_) return true;
    return commit_field();
}

auto urlencoded_decoder::commit_field() -> bool {
    if (fields_.size() >= max_fields_) {
        return fail(http::outcome_code::limit_exceeded, "form fields budget exhausted");
    }
    fields_.emplace_back(std::move(name_), std::move(value_));
    reset_token();
    return true;
}

auto urlencoded_decoder::append(char c) -> void {
    if (in_value_) {
        value_.push_back(c);
    } else {
        name_.push_back(c);
    }
}

auto urlencoded_decoder::reset_token() -> void {
    name_.clear();
    value_.clear();
    in_value_ = false;
    saw_token_byte_ = false;
}

auto urlencoded_decoder::hex_value(unsigned char c) noexcept -> unsigned char {
    if (c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a') return static_cast<unsigned char>(c - 'a' + 10);
    return static_cast<unsigned char>(c - 'A' + 10);
}

auto urlencoded_decoder::fail(http::outcome_code code, const char* what) -> bool {
    failure_ = http::outcome(code, std::string("urlencoded_decoder: ") + what);
    failed_ = true;
    return false;
}

}  // namespace httpserver::detail
