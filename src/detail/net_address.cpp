/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-119 step 1: the text conversion and parsing of the net
// address vocabulary (net/address.hpp). One platform divergence
// point, mirroring io_poll_sys.hpp's dispatch: inet_pton/inet_ntop
// come from arpa/inet.h on POSIX and ws2tcpip.h on Windows. The
// prefix-bits representation and this control flow are a fresh v3
// implementation -- no token-run is shared with the v2 octet-mask
// parser (detail/ip_representation.cpp), which stays untouched for
// the v2 surface.

#include <httpserver/net/address.hpp>

#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace httpserver {

namespace net {

namespace {

// Longest canonical literal: an IPv6 with an embedded IPv4 tail.
constexpr std::size_t k_max_literal = 45;

// The v4-mapped marker: bytes[0..9] zero and bytes[10..11] 0xff
// ("::ffff:0:0/96"). A mapped literal normalizes to family ipv4.
bool mapped_tail(const std::array<std::byte, 16>& bytes) noexcept {
    for (std::size_t i = 0; i < 10; ++i) {
        if (bytes[i] != std::byte{0}) return false;
    }
    return bytes[10] == std::byte{0xff} && bytes[11] == std::byte{0xff};
}

address from_v4_bytes(const unsigned char* raw) {
    address out;
    out.family = address_family::ipv4;
    std::memcpy(out.bytes.data() + 12, raw, 4);
    return out;
}

address from_v6_bytes(const unsigned char* raw) {
    address out;
    std::memcpy(out.bytes.data(), raw, 16);
    if (mapped_tail(out.bytes)) {
        // Equal to the plain IPv4 literal: the marker bytes zero out
        // so both spellings produce the same value.
        out.bytes.fill(std::byte{0});
        std::memcpy(out.bytes.data() + 12, raw + 12, 4);
        out.family = address_family::ipv4;
        return out;
    }
    out.family = address_family::ipv6;
    return out;
}

// NUL-terminates @p text into @p buf (sized for the longest literal);
// false when the text cannot fit, so the platform parser never reads
// past its input.
bool zcopy(std::string_view text, char (&buf)[k_max_literal + 1]) noexcept {
    if (text.empty() || text.size() > k_max_literal) return false;
    std::memcpy(buf, text.data(), text.size());
    buf[text.size()] = '\0';
    return true;
}

// Decimal prefix length ("/n"): one to three digits, no leading zero
// unless the value is exactly "0", at most @p limit. False otherwise.
bool parse_prefix_len(std::string_view digits, unsigned limit,
                      unsigned& out) noexcept {
    if (digits.empty() || digits.size() > 3) return false;
    if (digits.size() > 1 && digits[0] == '0') return false;
    unsigned value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return false;
        value = value * 10u + static_cast<unsigned>(c - '0');
    }
    if (value > limit) return false;
    out = value;
    return true;
}

// Splits "literal/n" once; false when '/' appears more than once or
// either side is empty (a dangling "/" or "/8" is malformed).
bool split_cidr(std::string_view text, std::string_view& literal,
                std::string_view& suffix) noexcept {
    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos) {
        literal = text;
        return true;
    }
    if (slash == 0 || text.find('/', slash + 1) != std::string_view::npos) {
        return false;
    }
    literal = text.substr(0, slash);
    suffix = text.substr(slash + 1);
    return !suffix.empty();
}

// The IPv4 mapped-space offset: a /n IPv4 pattern carries 96 + n.
constexpr unsigned k_v4_offset = 96;

// "a.b.c.*": exactly four segments with the last one '*'. The base
// literal replaces the wildcard with a zero; the prefix keeps the
// first three octets (96 + 24 bits).
std::optional<address_pattern> parse_v4_wildcard(std::string_view text) {
    if (!text.ends_with(".*")) return std::nullopt;
    const std::string literal(text.substr(0, text.size() - 2));
    const std::optional<address> base =
        parse_address(literal + ".0");
    if (!base.has_value()) return std::nullopt;
    // Reject any further wildcard inside the surviving segments
    // (the middle-wildcard delta).
    if (literal.find('*') != std::string::npos) return std::nullopt;
    address_pattern out;
    out.base = *base;
    out.prefix_bits = k_v4_offset + 24;
    return out;
}

// One IPv4 pattern: trailing wildcard, CIDR, or exact literal.
std::optional<address_pattern> parse_v4(std::string_view text) {
    if (const std::optional<address_pattern> wildcard =
            parse_v4_wildcard(text);
        wildcard.has_value()) {
        return wildcard;
    }
    std::string_view literal;
    std::string_view suffix;
    if (!split_cidr(text, literal, suffix)) return std::nullopt;
    const std::optional<address> base = parse_address(literal);
    if (!base.has_value()
            || base->family != address_family::ipv4) {
        return std::nullopt;
    }
    address_pattern out;
    out.base = *base;
    out.prefix_bits = k_v4_offset + 32;
    if (suffix.empty()) return out;  // exact literal
    unsigned bits = 0;
    if (!parse_prefix_len(suffix, 32, bits)) return std::nullopt;
    out.prefix_bits = k_v4_offset + bits;
    return out;
}

// One IPv6 pattern: exact literal or CIDR. Wildcards are not part of
// the IPv6 spelling (CIDR is the compression form). A v4-mapped
// literal normalizes to family ipv4 and stays an exact pattern.
std::optional<address_pattern> parse_v6(std::string_view text) {
    if (text.find('*') != std::string_view::npos) return std::nullopt;
    std::string_view literal;
    std::string_view suffix;
    if (!split_cidr(text, literal, suffix)) return std::nullopt;
    const std::optional<address> base = parse_address(literal);
    if (!base.has_value()) return std::nullopt;
    address_pattern out;
    out.base = *base;
    out.prefix_bits = 128;
    if (!suffix.empty()) {
        if (base->family != address_family::ipv6) return std::nullopt;
        unsigned bits = 0;
        if (!parse_prefix_len(suffix, 128, bits)) return std::nullopt;
        out.prefix_bits = bits;
        return out;
    }
    return out;
}

}  // namespace

std::optional<address> parse_address(std::string_view text) {
    char buf[k_max_literal + 1];
    if (!zcopy(text, buf)) return std::nullopt;
    if (text.find(':') == std::string_view::npos) {
        unsigned char raw[4];
        if (::inet_pton(AF_INET, buf, raw) == 1) {
            return from_v4_bytes(raw);
        }
        return std::nullopt;
    }
    unsigned char raw[16];
    if (::inet_pton(AF_INET6, buf, raw) == 1) {
        return from_v6_bytes(raw);
    }
    return std::nullopt;
}

std::optional<address_pattern> parse_pattern(std::string_view text) {
    if (text.find(':') == std::string_view::npos) {
        return parse_v4(text);
    }
    return parse_v6(text);
}

bool address_pattern::matches(const address& candidate) const noexcept {
    if (base.family == address_family::unspec) return false;
    if (candidate.family != base.family) return false;
    const unsigned whole = prefix_bits / 8;
    for (unsigned i = 0; i < whole; ++i) {
        if (base.bytes[i] != candidate.bytes[i]) return false;
    }
    const unsigned leftover = prefix_bits % 8;
    if (leftover == 0) return true;
    const unsigned mask = (0xffu << (8u - leftover)) & 0xffu;
    const unsigned want =
        static_cast<unsigned>(base.bytes[whole]) & mask;
    const unsigned have =
        static_cast<unsigned>(candidate.bytes[whole]) & mask;
    return want == have;
}

std::string address::to_string() const {
    if (family == address_family::ipv4) {
        char buf[INET_ADDRSTRLEN] = {0};
        unsigned char raw[4];
        std::memcpy(raw, bytes.data() + 12, 4);
        if (::inet_ntop(AF_INET, raw, buf, sizeof buf) != nullptr) {
            return std::string(buf);
        }
        return std::string();
    }
    if (family == address_family::ipv6) {
        char buf[INET6_ADDRSTRLEN] = {0};
        unsigned char raw[16];
        std::memcpy(raw, bytes.data(), 16);
        if (::inet_ntop(AF_INET6, raw, buf, sizeof buf) != nullptr) {
            return std::string(buf);
        }
        return std::string();
    }
    return std::string();
}

std::string peer_address::to_string() const {
    const std::string text = address.to_string();
    if (text.empty()) return text;
    if (address.family == address_family::ipv6) {
        return "[" + text + "]:" + std::to_string(port);
    }
    return text + ":" + std::to_string(port);
}

}  // namespace net

}  // namespace httpserver
