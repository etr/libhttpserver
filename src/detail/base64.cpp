/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/base64.hpp>
#include <string>
#include <vector>

namespace httpserver::detail {

void encode_quantum(std::string& out, std::uint32_t bits) {
    out.push_back(k_base64_alphabet[(bits >> 18) & 0x3f]);
    out.push_back(k_base64_alphabet[(bits >> 12) & 0x3f]);
    out.push_back(k_base64_alphabet[(bits >> 6) & 0x3f]);
    out.push_back(k_base64_alphabet[bits & 0x3f]);
}

std::string base64_encode(std::span<const std::byte> input) {
    std::string out;
    out.reserve((input.size() / 3 + 1) * 4);
    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        encode_quantum(out, (octet(input[i]) << 16) | (octet(input[i + 1]) << 8) | octet(input[i + 2]));
        i += 3;
    }
    const std::size_t rest = input.size() - i;
    if (rest == 1) {
        const std::uint32_t bits = octet(input[i]) << 16;
        out.push_back(k_base64_alphabet[(bits >> 18) & 0x3f]);
        out.push_back(k_base64_alphabet[(bits >> 12) & 0x3f]);
        out.append("==");
    } else if (rest == 2) {
        const std::uint32_t bits = (octet(input[i]) << 16) | (octet(input[i + 1]) << 8);
        out.push_back(k_base64_alphabet[(bits >> 18) & 0x3f]);
        out.push_back(k_base64_alphabet[(bits >> 12) & 0x3f]);
        out.push_back(k_base64_alphabet[(bits >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

int base64_value(const char c) noexcept {
    const std::size_t pos = k_base64_alphabet.find(c);
    return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
}

std::size_t base64_padding(std::string_view input) noexcept {
    if (input.empty() || input.back() != '=') return 0;
    if (input.size() >= 2 && input[input.size() - 2] == '=') return 2;
    return 1;
}

void decode_quantum(std::vector<std::byte>& out, std::uint32_t bits) {
    out.push_back(static_cast<std::byte>((bits >> 16) & 0xff));
    out.push_back(static_cast<std::byte>((bits >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(bits & 0xff));
}

bool decode_final_quantum(std::vector<std::byte>& out, std::uint32_t bits, int count) {
    if (count == 2) {
        if ((bits & 0xf) != 0) return false;
        out.push_back(static_cast<std::byte>(bits >> 4));
    } else if (count == 3) {
        if ((bits & 0x3) != 0) return false;
        out.push_back(static_cast<std::byte>(bits >> 10));
        out.push_back(static_cast<std::byte>((bits >> 2) & 0xff));
    }
    return true;
}

std::optional<std::vector<std::byte>> base64_decode(std::string_view input) {
    if (input.size() % 4 != 0) return std::nullopt;
    const std::size_t pad = base64_padding(input);
    std::vector<std::byte> out;
    out.reserve(input.size() / 4 * 3);
    std::uint32_t bits = 0;
    int count = 0;
    for (std::size_t i = 0; i < input.size() - pad; ++i) {
        const int value = base64_value(input[i]);
        if (value < 0) return std::nullopt;
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        if (++count == 4) {
            decode_quantum(out, bits);
            bits = 0;
            count = 0;
        }
    }
    if (!decode_final_quantum(out, bits, count)) return std::nullopt;
    return out;
}

}  // namespace httpserver::detail
