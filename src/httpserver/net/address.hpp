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

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-119 (plan D3/D5): the v3 network address vocabulary of the
// peer policy (PRD-V3N-REQ-014, DR-V3-001). Pure value types: an
// address family, one address (family plus network-order bytes), one
// peer snapshot (address plus host-order port), and one address
// pattern (base plus a prefix length). No transport, engine, or
// platform vocabulary appears here; text conversion and parsing live
// in the library's detail/ and are declared below as free functions.
//
// Representation rule: an IPv4 address lives right-aligned in the
// 16-byte array (the v4-mapped tail, octets 12..15), so both families
// compare over one byte range and one prefix length. A v4-mapped IPv6
// literal ("::ffff:127.0.0.1") parses to family ipv4, equal to
// "127.0.0.1".
//
// NOT part of the v2 umbrella <httpserver.hpp>: like the rest of the
// v3 server area this is an additive surface.

#ifndef SRC_HTTPSERVER_NET_ADDRESS_HPP_
#define SRC_HTTPSERVER_NET_ADDRESS_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace httpserver {

namespace net {

// The family of one network address. `unspec` is the absence of a
// peer snapshot (a rigged exchange, or an accept that reported no
// address); it never matches a pattern and a reject-all policy
// refuses it (peer_policy.hpp documents that rule).
enum class address_family : std::uint8_t {
    unspec,
    ipv4,
    ipv6,
};

// One network address: family plus the address bytes in network
// order (IPv4 right-aligned, see above).
struct address {
    address_family family = address_family::unspec;
    std::array<std::byte, 16> bytes{};

    // The address text without a port: dotted-quad for ipv4,
    // compressed form for ipv6, empty for unspec.
    std::string to_string() const;

    bool operator==(const address&) const = default;
    bool operator!=(const address&) const = default;
};

// One transport peer snapshot: the address plus the port in host
// byte order. Snapshotted once at accept and carried immutably
// afterwards (the exchange stamps it at construction; there is no
// setter -- a reconnect is a new connection with a new snapshot).
struct peer_address {
    net::address address{};
    std::uint16_t port = 0;  // host byte order

    // The endpoint text: "192.0.2.1:80" for ipv4, "[2001:db8::1]:80"
    // for ipv6; empty for an unspec address.
    std::string to_string() const;

    bool operator==(const peer_address&) const = default;
    bool operator!=(const peer_address&) const = default;
};

// One address pattern: a base address plus a prefix length in bits
// over the 16-byte (v4-mapped) form. IPv4 prefixes live in the
// mapped space: a /8 IPv4 pattern carries prefix_bits 96 + 8.
//
// Accepted spellings (parse_pattern):
//   "192.0.2.1"       exact IPv4 literal           (prefix 128)
//   "192.0.2.*"       trailing wildcard, IPv4 only (prefix 96 + 24)
//   "10.0.0.0/8"      IPv4 CIDR, /0 ../32
//   "2001:db8::1"     exact IPv6 literal
//   "2001:db8::/32"   IPv6 CIDR, /0 ../128
// A wildcard anywhere but the last dotted-quad segment is rejected:
// the v2 middle-wildcard behavior was an implementation accident and
// is a documented v3 delta (the migration notes name it). The IPv6
// wildcard-free form is CIDR.
struct address_pattern {
    net::address base{};
    unsigned prefix_bits = 0;  // 0..128 over the mapped form

    // True when @p candidate is the same family and shares the first
    // prefix_bits bits of the mapped form. An unspec base or
    // candidate never matches.
    bool matches(const address& candidate) const noexcept;

    bool operator==(const address_pattern&) const = default;
    bool operator!=(const address_pattern&) const = default;
};

// Parses one address literal: an IPv4 dotted-quad or an IPv6 literal
// with :: compression and an embedded IPv4 tail; a v4-mapped literal
// normalizes to family ipv4. Nullopt on any other spelling. Performs
// no I/O.
std::optional<address> parse_address(std::string_view text);

// Parses one pattern spelling (see address_pattern). Nullopt on any
// other spelling, on a middle wildcard, and on a prefix length
// beyond the family's bit count. Performs no I/O.
std::optional<address_pattern> parse_pattern(std::string_view text);

namespace detail {

// TASK-119: the one byte-level decode of an address value. The
// library's text parser (parse_address above) and the engine's
// accept-time transport capture both build their values through it,
// so the v4-mapped normalization rule (::ffff:0:0/96: bytes 0..9
// zero AND bytes 10..11 0xff) exists in exactly one place and the
// parsed spelling and the captured peer of one host can never
// disagree -- a genuine IPv6 address that merely carries 0xffff at
// bytes 10..11 stays family ipv6. ipv4 reads the leading 4 raw
// bytes (right-aligned into the 16-byte form); ipv6 reads 16; unspec
// yields the empty address. TU-defined in the library's detail/
// (the internal-bridge precedent of server/hooks.hpp and
// server/peer_policy.hpp; not part of the consumer surface).
address address_from_bytes(address_family family, const std::byte* raw);

}  // namespace detail

}  // namespace net

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_NET_ADDRESS_HPP_
