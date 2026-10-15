/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/digest_response.hpp>
#include <string>

namespace httpserver::detail::digest::response_detail {

std::span<const std::byte> raw_bytes(std::string_view text) noexcept {
    const auto* raw = reinterpret_cast<const std::byte*>(text.data());
    return std::span<const std::byte>(raw, text.size());
}

std::string colon_joined(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (const std::string_view part : parts) {
        if (!out.empty()) out.push_back(':');
        out.append(part);
    }
    return out;
}

}  // namespace httpserver::detail::digest::response_detail
