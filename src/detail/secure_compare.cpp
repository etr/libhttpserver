/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/secure_compare.hpp>

namespace httpserver::detail {

bool constant_time_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
    }
    return diff == 0;
}

}  // namespace httpserver::detail
