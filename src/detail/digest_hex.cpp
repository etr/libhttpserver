/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/digest_hex.hpp>
#include <string>
#include <vector>

namespace httpserver::detail::digest {

std::string encode_hex(std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        const unsigned value = static_cast<unsigned>(b);
        out.push_back(digits[(value >> 4) & 0xf]);
        out.push_back(digits[value & 0xf]);
    }
    return out;
}

}  // namespace httpserver::detail::digest

namespace httpserver::detail::digest::hex_detail {

int hex_digit_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace httpserver::detail::digest::hex_detail

namespace httpserver::detail::digest {

std::optional<std::vector<std::byte>> decode_hex(std::string_view text) {
    using hex_detail::hex_digit_value;
    if (text.empty() || text.size() % 2 != 0) return std::nullopt;
    std::vector<std::byte> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = hex_digit_value(text[i]);
        const int low = hex_digit_value(text[i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<std::byte>((high << 4) | low));
    }
    return out;
}

}  // namespace httpserver::detail::digest
