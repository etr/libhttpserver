/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/auth_text.hpp>

namespace httpserver::detail::auth_text {

bool ascii_iequal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (lowered(left[i]) != lowered(right[i])) return false;
    }
    return true;
}

}  // namespace httpserver::detail::auth_text
