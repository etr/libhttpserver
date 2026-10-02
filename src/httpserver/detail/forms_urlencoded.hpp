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

// TASK-116: the strict incremental application/x-www-form-urlencoded
// body decoder (PRD-V3N-REQ-021/022, v2 within-cap parity per
// PRD-V3N-REQ-038, DR-V3-001). NOT part of the installed surface.
//
// Wire semantics (v2's documented contract, reproduced within cap):
// '&' separates pairs; the FIRST '=' in a pair separates name from
// value (later '='s are value data); a token with no '=' is a name
// with an empty value; empty tokens (a&&b, trailing '&') are skipped;
// '+' is 0x20; a complete case-insensitive %HH is the decoded byte;
// every other raw byte -- NUL and high-bit included -- passes through
// verbatim (values are length-carrying strings, so a decoded %00 is
// storable). Repeated names append in arrival order; first-value
// lookup is the caller's rule. Decoded bytes never re-enter the pair
// state machine: a %26 is data, never a separator.
//
// The TASK-116 deltas over v2 (migration-noted): an incomplete or
// non-hex %HH -- including one truncated at end of body and one split
// across feed boundaries -- is a typed invalid_argument rejection,
// where v2 passed the bytes through literally; the byte and field
// budgets are typed limit_exceeded rejections, where v2 truncated
// silently.
//
// Bounded admission: max_total_bytes counts RAW body bytes consumed
// and max_fields counts decoded pairs emitted, so decoded storage is
// bounded by construction (decoded bytes <= raw bytes <= cap, plus
// the field count cap). Exactly-at-cap succeeds. A field materializes
// only when its token flushes (at a separator or finish()), so the
// stored prefix provably stops at the failing feed: the pending token
// and every later pair never materialize. Rejections are sticky: a
// failed decoder refuses further feeds and finish() with the same
// typed outcome.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/forms_urlencoded.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_FORMS_URLENCODED_HPP_
#define SRC_HTTPSERVER_DETAIL_FORMS_URLENCODED_HPP_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

// Incremental decoder for one urlencoded request body. Not
// thread-safe: the caller feeds it on the reader's thread. State
// survives arbitrary feed boundaries (the escape accumulator included,
// the classic urlencoded-parser bug), so streaming feeds and one-shot
// feeds decode identically.
class urlencoded_decoder {
 public:
    urlencoded_decoder(std::uint64_t max_total_bytes,
                       std::uint64_t max_fields) noexcept
        : max_total_bytes_(max_total_bytes), max_fields_(max_fields) { }

    // Decodes a prefix of the raw body. Bytes before a failing byte
    // are consumed into the token state (the stored prefix stops at
    // the failure); after a typed failure every later feed returns the
    // same outcome and consumes nothing.
    http::outcome feed(std::span<const std::byte> wire) {
        if (failed_) return failure_;
        for (const std::byte b : wire) {
            if (bytes_seen_ >= max_total_bytes_) {
                fail(http::outcome_code::limit_exceeded,
                     "raw body bytes budget exhausted");
                return failure_;
            }
            ++bytes_seen_;
            if (!feed_byte(static_cast<unsigned char>(b))) {
                return failure_;
            }
        }
        return http::outcome::okay();
    }

    // Ends the body: flushes a pending token as a field (budget
    // checked) and rejects an escape still in progress (truncated at
    // end of body). Idempotent after a failure.
    http::outcome finish() {
        if (failed_) return failure_;
        if (escaping_) {
            fail(http::outcome_code::invalid_argument,
                 "%HH escape truncated at end of body");
            return failure_;
        }
        if (saw_token_byte_ && !commit_field()) return failure_;
        return http::outcome::okay();
    }

    // The decoded fields in arrival order (repeated names appended);
    // the pending token of an unfinished or failed decode is not
    // included. Moves the storage out.
    std::vector<std::pair<std::string, std::string>> take_fields() {
        return std::move(fields_);
    }

 private:
    // One wire byte into the token state machine. Order matters: an
    // in-progress escape sees every byte as its next digit (a
    // separator cutting an escape short is malformed), and '&' itself
    // is never a token byte.
    bool feed_byte(unsigned char c) {
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

    // Strict %HH: both digits must be case-insensitive hex; anything
    // else is the typed malformed rejection.
    bool feed_escape(unsigned char c) {
        if (!is_hex(c)) {
            return fail(http::outcome_code::invalid_argument,
                        "malformed %HH escape");
        }
        escape_value_ = escape_value_ * 16 + hex_value(c);
        if (++escape_digits_ == 2) {
            escaping_ = false;
            append(static_cast<char>(escape_value_));
        }
        return true;
    }

    // A separator: a token that consumed at least one wire byte is a
    // field; an empty token is skipped and never consumes field
    // budget.
    bool flush_token() {
        if (!saw_token_byte_) return true;
        return commit_field();
    }

    // Emits the pending token as a field. The budget check precedes
    // the append, so the over-cap pair never materializes.
    bool commit_field() {
        if (fields_.size() >= max_fields_) {
            return fail(http::outcome_code::limit_exceeded,
                        "form fields budget exhausted");
        }
        fields_.emplace_back(std::move(name_), std::move(value_));
        reset_token();
        return true;
    }

    void append(char c) {
        if (in_value_) {
            value_.push_back(c);
        } else {
            name_.push_back(c);
        }
    }

    void reset_token() {
        name_.clear();
        value_.clear();
        in_value_ = false;
        saw_token_byte_ = false;
    }

    static bool is_hex(unsigned char c) noexcept {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
    }

    static unsigned char hex_value(unsigned char c) noexcept {
        if (c <= '9') return static_cast<unsigned char>(c - '0');
        if (c >= 'a') return static_cast<unsigned char>(c - 'a' + 10);
        return static_cast<unsigned char>(c - 'A' + 10);
    }

    // Records the typed, sticky rejection and freezes the stored
    // prefix. Returns false so byte handlers can `return fail(...)`.
    bool fail(http::outcome_code code, const char* what) {
        failure_ = http::outcome(
            code, std::string("urlencoded_decoder: ") + what);
        failed_ = true;
        return false;
    }

    std::uint64_t max_total_bytes_;
    std::uint64_t max_fields_;
    std::vector<std::pair<std::string, std::string>> fields_;
    std::string name_;
    std::string value_;
    http::outcome failure_;
    std::uint64_t bytes_seen_ = 0;
    unsigned char escape_value_ = 0;
    unsigned escape_digits_ = 0;
    bool in_value_ = false;
    bool saw_token_byte_ = false;
    bool escaping_ = false;
    bool failed_ = false;
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_FORMS_URLENCODED_HPP_
