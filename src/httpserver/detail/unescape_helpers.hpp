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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// Internal-only helpers for URL percent-decoding.
//
// Shared between src/http_utils.cpp (http_unescape) and
// src/detail/http_request_impl.cpp (arena-routed unescape).
// Not part of the public API; not installed.
//
// TASK-072: extracted from the two translation units that previously
// each maintained a private copy.  Having one source of truth ensures
// that bug-fixes and RFC-3986 edge-case corrections propagate uniformly.
// (code-simplifier-iter1-1, code-simplifier-iter1-2)

#pragma once

#include <cstddef>

namespace httpserver {
namespace detail {

// Map a single hex character to its integer value [0, 15].
// Returns -1 for any character that is not a valid hex digit.
//
// constexpr so callers in constant-expression contexts can fold the
// table at compile time; trivially inlined at -O1 and above.
[[nodiscard]] constexpr int hex_digit_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decode and '+'-to-space unescape a raw byte buffer in-place.
//
// Reads from [data, data+size), writes the decoded bytes back into the
// same buffer starting at data[0], and returns the new (decoded) length.
// The output length is always <= the input length because %HH sequences
// (3 bytes) decode to 1 byte and '+' stays 1 byte.
//
// The loop stops at a '\0' byte or when rpos reaches size, whichever
// comes first, so embedded null bytes in the input are treated as
// terminators (matching the behaviour of the original http_unescape).
//
// The caller is responsible for adding any terminating '\0' after the
// returned length if the buffer must be C-string-compatible.
inline std::size_t unescape_buf_raw(char* data, std::size_t size) noexcept {
    if (size == 0) return 0;
    std::size_t rpos = 0;
    std::size_t wpos = 0;
    while (rpos < size && data[rpos] != '\0') {
        switch (data[rpos]) {
            case '+':
                data[wpos++] = ' ';
                ++rpos;
                break;
            case '%':
                // Overflow-safe bound: rpos < size here, so size - rpos
                // cannot underflow, unlike the `size > rpos + 2` form
                // which would wrap if rpos were ever near SIZE_MAX.
                if (size - rpos > 2) {
                    const int hi = hex_digit_value(data[rpos + 1]);
                    const int lo = hex_digit_value(data[rpos + 2]);
                    if (hi >= 0 && lo >= 0) {
                        data[wpos++] =
                            static_cast<char>((hi << 4) | lo);
                        rpos += 3;
                        break;
                    }
                }
            // intentional fall through!
            default:
                data[wpos++] = data[rpos++];
        }
    }
    return wpos;
}

}  // namespace detail
}  // namespace httpserver
