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

// TASK-119 step 1: the net address vocabulary (net/address.hpp):
// literal parsing across families, v4-mapped normalization, the
// pattern spellings (exact literal, trailing wildcard, CIDR over
// both families), the middle-wildcard rejection (the documented v3
// delta), family isolation of matches, and the text round trips.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <httpserver/net/address.hpp>

#include "./littletest.hpp"

namespace net = httpserver::net;

using net::address;
using net::address_family;
using net::address_pattern;
using net::peer_address;

std::optional<address> parsed(const char* text) {
    return net::parse_address(text);
}

std::optional<address_pattern> pattern(const char* text) {
    return net::parse_pattern(text);
}

LT_BEGIN_SUITE(net_address_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(net_address_suite)

LT_BEGIN_AUTO_TEST(net_address_suite, parses_v4_literal_into_mapped_tail)
    const std::optional<address> a = parsed("127.0.0.1");
    LT_CHECK(a.has_value());
    if (!a.has_value()) return;
    LT_CHECK(a->family == address_family::ipv4);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[0]), 0u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[11]), 0u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[12]), 127u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[13]), 0u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[14]), 0u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[15]), 1u);
LT_END_AUTO_TEST(parses_v4_literal_into_mapped_tail)

LT_BEGIN_AUTO_TEST(net_address_suite, parses_v6_literal)
    const std::optional<address> a = parsed("2001:db8::1");
    LT_CHECK(a.has_value());
    if (!a.has_value()) return;
    LT_CHECK(a->family == address_family::ipv6);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[0]), 0x20u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[1]), 0x01u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[2]), 0x0du);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[3]), 0xb8u);
    LT_CHECK_EQ(static_cast<unsigned>(a->bytes[15]), 1u);
LT_END_AUTO_TEST(parses_v6_literal)

LT_BEGIN_AUTO_TEST(net_address_suite, v4_mapped_literal_normalizes_to_v4)
    const std::optional<address> mapped = parsed("::ffff:127.0.0.1");
    const std::optional<address> plain = parsed("127.0.0.1");
    LT_CHECK(mapped.has_value());
    LT_CHECK(plain.has_value());
    if (!mapped.has_value() || !plain.has_value()) return;
    LT_CHECK(mapped->family == address_family::ipv4);
    LT_CHECK(*mapped == *plain);
LT_END_AUTO_TEST(v4_mapped_literal_normalizes_to_v4)

LT_BEGIN_AUTO_TEST(net_address_suite, rejects_bad_literals)
    LT_CHECK(!parsed("").has_value());
    LT_CHECK(!parsed("127.0.0").has_value());
    LT_CHECK(!parsed("127.0.0.1.5").has_value());
    LT_CHECK(!parsed("256.1.1.1").has_value());
    LT_CHECK(!parsed("abc").has_value());
    LT_CHECK(!parsed("127.0.0.1:8080").has_value());
    LT_CHECK(!parsed("::ffff:127.0.0.1:9").has_value());
    LT_CHECK(!parsed("2001:db8:::1").has_value());
LT_END_AUTO_TEST(rejects_bad_literals)

LT_BEGIN_AUTO_TEST(net_address_suite, address_text_round_trips)
    LT_CHECK(parsed("127.0.0.1").value_or(address{}).to_string()
             == "127.0.0.1");
    LT_CHECK(parsed("192.168.1.100").value_or(address{}).to_string()
             == "192.168.1.100");
    LT_CHECK(parsed("::1").value_or(address{}).to_string() == "::1");
    LT_CHECK(parsed("2001:db8::1").value_or(address{}).to_string()
             == "2001:db8::1");
    LT_CHECK(parsed("2001:db8:0:1:1:1:1:1").value_or(address{}).to_string()
             == "2001:db8:0:1:1:1:1:1");
    LT_CHECK(address{}.to_string().empty());
LT_END_AUTO_TEST(address_text_round_trips)

LT_BEGIN_AUTO_TEST(net_address_suite, peer_address_endpoint_text)
    peer_address v4;
    v4.address = parsed("192.0.2.1").value_or(address{});
    v4.port = 80;
    LT_CHECK(v4.to_string() == "192.0.2.1:80");
    peer_address v6;
    v6.address = parsed("2001:db8::1").value_or(address{});
    v6.port = 8080;
    LT_CHECK(v6.to_string() == "[2001:db8::1]:8080");
    LT_CHECK(peer_address{}.to_string().empty());
    LT_CHECK(peer_address{}.port == 0);
LT_END_AUTO_TEST(peer_address_endpoint_text)

LT_BEGIN_AUTO_TEST(net_address_suite, trailing_wildcard_keeps_three_octets)
    const std::optional<address_pattern> p = pattern("127.0.0.*");
    LT_CHECK(p.has_value());
    if (!p.has_value()) return;
    LT_CHECK(p->base.family == address_family::ipv4);
    LT_CHECK_EQ(p->prefix_bits, 120u);
    LT_CHECK(p->matches(parsed("127.0.0.1").value_or(address{})));
    LT_CHECK(p->matches(parsed("127.0.0.254").value_or(address{})));
    LT_CHECK(!p->matches(parsed("127.0.1.1").value_or(address{})));
    LT_CHECK(!p->matches(parsed("128.0.0.1").value_or(address{})));
LT_END_AUTO_TEST(trailing_wildcard_keeps_three_octets)

LT_BEGIN_AUTO_TEST(net_address_suite, middle_wildcards_are_rejected)
    LT_CHECK(!pattern("127.*.0.1").has_value());
    LT_CHECK(!pattern("*.0.0.1").has_value());
    LT_CHECK(!pattern("*.*.*.*").has_value());
    LT_CHECK(!pattern("127.0.0.*.1").has_value());
    LT_CHECK(!pattern("127.0.*").has_value());
    LT_CHECK(!pattern("2001:db8::*").has_value());
LT_END_AUTO_TEST(middle_wildcards_are_rejected)

LT_BEGIN_AUTO_TEST(net_address_suite, v4_cidr_including_bit_aligned)
    const std::optional<address_pattern> byte_aligned = pattern("10.0.0.0/8");
    LT_CHECK(byte_aligned.has_value());
    if (byte_aligned.has_value()) {
        LT_CHECK_EQ(byte_aligned->prefix_bits, 104u);
        LT_CHECK(byte_aligned->matches(parsed("10.1.2.3")
                                           .value_or(address{})));
        LT_CHECK(!byte_aligned->matches(parsed("11.0.0.1")
                                            .value_or(address{})));
    }
    const std::optional<address_pattern> split = pattern("10.1.2.3/13");
    LT_CHECK(split.has_value());
    if (split.has_value()) {
        LT_CHECK_EQ(split->prefix_bits, 109u);
        LT_CHECK(split->matches(parsed("10.7.200.1").value_or(address{})));
        LT_CHECK(!split->matches(parsed("10.8.0.1").value_or(address{})));
    }
    const std::optional<address_pattern> all = pattern("0.0.0.0/0");
    LT_CHECK(all.has_value());
    if (all.has_value()) {
        LT_CHECK(all->matches(parsed("203.0.113.9").value_or(address{})));
    }
LT_END_AUTO_TEST(v4_cidr_including_bit_aligned)

LT_BEGIN_AUTO_TEST(net_address_suite, v6_cidr_and_exact)
    const std::optional<address_pattern> cidr = pattern("2001:db8::/32");
    LT_CHECK(cidr.has_value());
    if (cidr.has_value()) {
        LT_CHECK_EQ(cidr->prefix_bits, 32u);
        LT_CHECK(cidr->matches(parsed("2001:db8::1").value_or(address{})));
        LT_CHECK(cidr->matches(parsed("2001:db8:ffff::1")
                                  .value_or(address{})));
        LT_CHECK(!cidr->matches(parsed("2001:db9::").value_or(address{})));
    }
    const std::optional<address_pattern> exact = pattern("2001:db8::1");
    LT_CHECK(exact.has_value());
    if (exact.has_value()) {
        LT_CHECK_EQ(exact->prefix_bits, 128u);
        LT_CHECK(exact->matches(parsed("2001:db8::1").value_or(address{})));
        LT_CHECK(!exact->matches(parsed("2001:db8::2").value_or(address{})));
    }
    const std::optional<address_pattern> any = pattern("::/0");
    LT_CHECK(any.has_value());
    if (any.has_value()) {
        LT_CHECK(any->matches(parsed("2001:db8::1").value_or(address{})));
        LT_CHECK(any->matches(parsed("::1").value_or(address{})));
    }
LT_END_AUTO_TEST(v6_cidr_and_exact)

LT_BEGIN_AUTO_TEST(net_address_suite, exact_v4_literal_is_full_prefix)
    const std::optional<address_pattern> p = pattern("192.0.2.1");
    LT_CHECK(p.has_value());
    if (!p.has_value()) return;
    LT_CHECK_EQ(p->prefix_bits, 128u);
    LT_CHECK(p->matches(parsed("192.0.2.1").value_or(address{})));
    LT_CHECK(!p->matches(parsed("192.0.2.2").value_or(address{})));
LT_END_AUTO_TEST(exact_v4_literal_is_full_prefix)

LT_BEGIN_AUTO_TEST(net_address_suite, matches_stay_within_their_family)
    const address_pattern v4 = pattern("127.0.0.1").value_or(
        address_pattern{});
    const address_pattern v6 = pattern("::ffff:127.0.0.1").value_or(
        address_pattern{});
    // The mapped literal normalized to ipv4: it is an exact v4 pattern.
    LT_CHECK(v6.base.family == address_family::ipv4);
    const address v6_peer = parsed("::1").value_or(address{});
    LT_CHECK(!v4.matches(v6_peer));
    const address_pattern v6_cidr =
        pattern("::/0").value_or(address_pattern{});
    LT_CHECK(!v6_cidr.matches(parsed("127.0.0.1").value_or(address{})));
    // unspec never matches anything.
    const address_pattern from_unspec;
    LT_CHECK(from_unspec.base.family == address_family::unspec);
    LT_CHECK(!from_unspec.matches(parsed("127.0.0.1")
                                      .value_or(address{})));
    LT_CHECK(!v4.matches(address{}));
LT_END_AUTO_TEST(matches_stay_within_their_family)

LT_BEGIN_AUTO_TEST(net_address_suite, rejects_bad_patterns)
    LT_CHECK(!pattern("").has_value());
    LT_CHECK(!pattern("10.0.0.0/33").has_value());
    LT_CHECK(!pattern("10.0.0.0/129").has_value());
    LT_CHECK(!pattern("2001:db8::/129").has_value());
    LT_CHECK(!pattern("10.0.0.0/").has_value());
    LT_CHECK(!pattern("10.0.0.0/abc").has_value());
    LT_CHECK(!pattern("10.0.0.0/-1").has_value());
    LT_CHECK(!pattern("10.0.0.0/8/8").has_value());
    LT_CHECK(!pattern("10.0.0.0/08").has_value());
    LT_CHECK(!pattern("2001:db8::1/32/16").has_value());
LT_END_AUTO_TEST(rejects_bad_patterns)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()