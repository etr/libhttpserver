/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/unescape_helpers.hpp>

namespace httpserver::detail {

std::size_t unescape_buf_raw(char* data, std::size_t size) noexcept {
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
                        data[wpos++] = static_cast<unsigned char>((hi << 4) | lo);
                        rpos += 3;
                        break;
                    }
                }
                [[fallthrough]];
            default:
                data[wpos++] = data[rpos++];
        }
    }
    return wpos;
}

}  // namespace httpserver::detail
