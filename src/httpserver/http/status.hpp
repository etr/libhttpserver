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

#ifndef SRC_HTTPSERVER_HTTP_STATUS_HPP_
#define SRC_HTTPSERVER_HTTP_STATUS_HPP_

#include <cstdint>

namespace httpserver {

namespace http {

// Semantic response status value (RFC 9110 §15). A status stores the
// numeric code exactly as given; valid() reports whether the code lies
// in the defined 100..599 range. The default-constructed value and any
// out-of-range code are invalid: every consumer of a response must
// check valid() before committing the value to the wire. Library-owned
// (DR-V3-001): no backend or OS status enumerations.
class status {
 public:
    // Constructs the invalid status (code 0).
    constexpr status() noexcept = default;

    // Wraps a numeric code. The value is stored as given even when it
    // is outside the valid range, so callers can diagnose what they
    // were handed; valid() gates use.
    static constexpr status from_code(std::uint16_t code) noexcept {
        return status(code);
    }

    // The stored numeric code, valid or not.
    constexpr std::uint16_t code() const noexcept { return code_; }

    // True iff the code lies in 100..599 inclusive.
    constexpr bool valid() const noexcept {
        return code_ >= min_code && code_ <= max_code;
    }

    // RFC 9110 category predicates. Exactly one is true for a valid
    // status; all five are false for an invalid one.
    constexpr bool informational() const noexcept {
        return valid() && code_ < 200;
    }

    constexpr bool success() const noexcept {
        return valid() && code_ >= 200 && code_ < 300;
    }

    constexpr bool redirection() const noexcept {
        return valid() && code_ >= 300 && code_ < 400;
    }

    constexpr bool client_error() const noexcept {
        return valid() && code_ >= 400 && code_ < 500;
    }

    constexpr bool server_error() const noexcept {
        return valid() && code_ >= 500;
    }

    friend constexpr bool operator==(const status& a, const status& b) noexcept {
        return a.code_ == b.code_;
    }

    friend constexpr bool operator!=(const status& a, const status& b) noexcept {
        return !(a == b);
    }

 private:
    static constexpr std::uint16_t min_code = 100;
    static constexpr std::uint16_t max_code = 599;

    constexpr explicit status(std::uint16_t code) noexcept : code_(code) { }

    std::uint16_t code_ = 0;
};

}  // namespace http

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_STATUS_HPP_
