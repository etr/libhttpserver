/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/hpack_field_section.hpp>

namespace httpserver::detail {

auto hpack_section_result::materialize() const -> http::fields {
    http::fields out;
    for (const auto& field : fields) out.append(field.name, field.value);
    return out;
}

hpack_integer_result hpack_inspect_string(std::span<const std::uint8_t> input, hpack_string_limits limits) noexcept {
    const auto length = hpack_decode_integer(input, 7, {});
    if (!length.status.ok()) return length;
    if (length.value > limits.max_encoded_bytes || length.value > SIZE_MAX) return {{hpack_state::limit_exceeded}};
    const auto size = static_cast<std::size_t>(length.value);
    if (size > input.size() - length.consumed) return {{hpack_state::incomplete}};
    if ((input[0] & 0x80) == 0) {
        if (size > limits.max_decoded_bytes) return {{hpack_state::limit_exceeded}};
        return {{}, size, length.consumed + size};
    }
    auto result = hpack_codec::walk_huffman(input.subspan(length.consumed, size), limits.max_decoded_bytes, nullptr);
    if (result.status.ok()) result.consumed = length.consumed + size;
    return result;
}

bool hpack_admit_field(std::size_t name, std::size_t value, std::size_t& remaining) noexcept {
    std::size_t size = 0;
    if (!hpack_entry_size(name, value, size) || size > remaining) return false;
    remaining -= size;
    return true;
}

}  // namespace httpserver::detail
