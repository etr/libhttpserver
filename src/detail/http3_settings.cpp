/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/http3_connection.hpp>
namespace httpserver::detail {
bool http3_connection::settings(std::span<const std::byte> payload, std::uint64_t id) {
    http3_peer_settings staged;
    std::array<std::uint64_t, 64> seen{};
    std::size_t count = 0;
    quic_cursor cursor(payload);
    while (cursor.remaining()) {
        const auto key = cursor.varint(); const auto value = cursor.varint();
        if (cursor.code() != quic_codec_code::ok) {
            fail(0x106, "Truncated SETTINGS pair", id); return false;
        }
        if (std::find(seen.begin(), seen.begin() + count, key) != seen.begin() + count || (key >= 2 && key <= 5)) {
            fail(0x109, "Duplicate or HTTP/2 reserved setting", id); return false;
        }
        if (count == std::min(limits_.settings_identifiers, seen.size())) {
            fail(0x107, "SETTINGS identifier limit", id); return false;
        }
        seen[count++] = key;
        if (key == 1) staged.qpack_capacity = value;
        if (key == 6) staged.max_field_section = value;
        if (key == 7) staged.qpack_blocked = value;
    }
    staged.received = true; peer_ = staged;
    return true;
}
std::optional<http3_error> http3_connection::attach_local_stream(http3_role role, std::uint64_t id) {
    if (error_) return error_;
    if (role < http3_role::control || role > http3_role::qpack_decoder || id > k_quic_max_integer || (id & 3) != 3)
        return fail(0x103, "Invalid local critical stream", id);
    const auto slot = static_cast<std::size_t>(role) - static_cast<std::size_t>(http3_role::control);
    if (local_ids_[slot] == id) return {};
    if (local_ids_[slot] || std::find(local_ids_.begin(), local_ids_.end(), id) != local_ids_.end())
        return fail(0x103, "Local critical stream initialized twice", id);
    local_ids_[slot] = id;
    return {};
}
std::span<const std::byte> http3_connection::local_prefix(http3_role role) const {
    if (error_) return {};
    if (role == http3_role::control) return std::span(control_prefix_).subspan(prefix_positions_[0]);
    if (role == http3_role::qpack_encoder) return std::span(encoder_prefix_).subspan(prefix_positions_[1]);
    if (role == http3_role::qpack_decoder) return std::span(decoder_prefix_).subspan(prefix_positions_[2]);
    return {};
}
bool http3_connection::advance_local_prefix(http3_role role, std::size_t count) {
    if (role < http3_role::control || role > http3_role::qpack_decoder) return false;
    const auto slot = static_cast<std::size_t>(role) - static_cast<std::size_t>(http3_role::control);
    if (count > local_prefix(role).size()) return false;
    prefix_positions_[slot] += count;
    return true;
}
}  // namespace httpserver::detail
