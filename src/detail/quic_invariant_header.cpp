/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/quic_invariant_header.hpp>
namespace httpserver {
namespace detail {
bool quic_cid::operator==(const quic_cid& other) const noexcept {
    return size <= k_quic_cid_bytes && size == other.size
        && std::equal(bytes.begin(), bytes.begin() + size, other.bytes.begin());
}
std::optional<quic_invariant_header> extract_quic_invariant_header(
    std::span<const std::byte> bytes, std::optional<std::size_t> short_cid_length) {
    if (bytes.empty()) return std::nullopt;
    quic_invariant_header result;
    const auto first = std::to_integer<unsigned>(bytes[0]);
    result.long_header = (first & 0x80) != 0;
    if (!result.long_header) {
        if (!(first & 0x40) || !short_cid_length || *short_cid_length > k_quic_cid_bytes
            || *short_cid_length > bytes.size() - 1) return std::nullopt;
        result.destination.size = *short_cid_length;
        std::copy_n(bytes.begin() + 1, result.destination.size, result.destination.bytes.begin());
        return result;
    }
    if (bytes.size() < 6) return std::nullopt;
    for (std::size_t index = 1; index <= 4; ++index) {
        result.version = (result.version << 8) | std::to_integer<std::uint32_t>(bytes[index]);
    }
    std::size_t cursor = 5;
    for (auto* cid : {&result.destination, &result.source}) {
        if (cursor >= bytes.size()) return std::nullopt;
        cid->size = std::to_integer<std::size_t>(bytes[cursor++]);
        if (cid->size > k_quic_cid_bytes || cid->size > bytes.size() - cursor) return std::nullopt;
        std::copy_n(bytes.begin() + cursor, cid->size, cid->bytes.begin());
        cursor += cid->size;
    }
    return result;
}
}  // namespace detail
}  // namespace httpserver
