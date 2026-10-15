/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <cstring>
#include <httpserver/detail/net_address.hpp>
namespace httpserver::net {
namespace {

// Longest canonical literal: an IPv6 with an embedded IPv4 tail.

// The v4-mapped marker: bytes[0..9] zero and bytes[10..11] 0xff
// ("::ffff:0:0/96"). A mapped byte string normalizes to family ipv4.
bool mapped_tail(const std::array<std::byte, 16>& bytes) noexcept {
    for (std::size_t i = 0; i < 10; ++i) {
        if (bytes[i] != std::byte{0}) return false;
    }
    return bytes[10] == std::byte{0xff} && bytes[11] == std::byte{0xff};
}

}  // namespace

namespace detail {

// The ONE byte-level decode of the address vocabulary (TASK-119):
// parse_address (the text path) and the engine's accept-time
// transport capture (io_poll_sys.hpp fill_peer, the byte path) both
// build their values here, so the v4-mapped rule above exists in
// exactly one place and one host's parsed spelling and captured peer
// can never disagree. ipv4 reads the leading 4 bytes right-aligned;
// ipv6 reads 16 and normalizes only a fully mapped byte string to
// family ipv4 (a genuine IPv6 address that merely carries 0xffff at
// bytes[10..11] stays ipv6); unspec yields the empty address.
address address_from_bytes(address_family family, const std::byte* raw) {
    address out;
    if (family == address_family::ipv4) {
        out.family = address_family::ipv4;
        std::memcpy(out.bytes.data() + 12, raw, 4);
        return out;
    }
    if (family == address_family::ipv6) {
        std::memcpy(out.bytes.data(), raw, 16);
        if (mapped_tail(out.bytes)) {
            // Equal to the plain IPv4 literal: the marker bytes zero
            // out so both spellings produce the same value.
            out.bytes.fill(std::byte{0});
            std::memcpy(out.bytes.data() + 12, raw + 12, 4);
            out.family = address_family::ipv4;
            return out;
        }
        out.family = address_family::ipv6;
    }
    return out;
}

}  // namespace detail

}  // namespace httpserver::net
