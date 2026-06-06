/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-065: RFC 5952 §4 canonicalization for peer_address::to_string()
// over IPv6 inputs. Pre-TASK-065 the IPv6 branch printed all eight
// 16-bit groups uncompressed (e.g. "2001:db8:0:0:0:0:0:1"); this test
// pins the §4.2.2 examples, the §4.3 "single zero MUST NOT collapse"
// rule, the §4.2.3 first-occurrence tie-break, and the §5 IPv4-mapped
// dotted-quad form. The IPv4 branch (unchanged by TASK-065) is also
// regression-pinned so a future tweak to the canonicalizer cannot
// regress the v4 path.
//
// Pure-host CPU test: constructs `peer_address` aggregates directly --
// no MHD round-trip, no socket headers.

#include <array>
#include <cstdint>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::peer_address;

namespace {

peer_address make_v6(const std::array<std::uint8_t, 16>& b) {
    peer_address p{};
    p.fam = peer_address::family::ipv6;
    p.bytes = b;
    return p;
}

peer_address make_v4(std::uint8_t a, std::uint8_t b,
                     std::uint8_t c, std::uint8_t d) {
    peer_address p{};
    p.fam = peer_address::family::ipv4;
    p.bytes[0] = a;
    p.bytes[1] = b;
    p.bytes[2] = c;
    p.bytes[3] = d;
    return p;
}

}  // namespace

LT_BEGIN_SUITE(peer_address_to_string_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(peer_address_to_string_suite)

// §4.2.2: leading-zero suppression + longest-run collapse.
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, rfc5952_documentation_example)
    // 2001:0db8:0000:0000:0000:0000:0000:0001 -> "2001:db8::1"
    auto p = make_v6({0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                      0,    0,    0,    0,    0, 0, 0, 0x01});
    LT_CHECK_EQ(p.to_string(), std::string{"2001:db8::1"});
LT_END_AUTO_TEST(rfc5952_documentation_example)

// §4.2.2: loopback.
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, rfc5952_loopback)
    auto p = make_v6({0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0x01});
    LT_CHECK_EQ(p.to_string(), std::string{"::1"});
LT_END_AUTO_TEST(rfc5952_loopback)

// §4.2.2: unspecified all-zero address renders as "::".
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, rfc5952_all_zero)
    auto p = make_v6({0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0});
    LT_CHECK_EQ(p.to_string(), std::string{"::"});
LT_END_AUTO_TEST(rfc5952_all_zero)

// §4.3: a single 16-bit zero group MUST NOT be shortened.
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, rfc5952_single_zero_no_collapse)
    // Groups: 2001:db8:0:1:1:1:1:1
    auto p = make_v6({0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0x01,
                      0,    0x01, 0,    0x01, 0, 0x01, 0, 0x01});
    LT_CHECK_EQ(p.to_string(), std::string{"2001:db8:0:1:1:1:1:1"});
LT_END_AUTO_TEST(rfc5952_single_zero_no_collapse)

// §4.2.3 tie-break: longest run wins (3-group run over 2-group run).
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, rfc5952_longest_run_wins)
    // Groups: 2001:0:0:1:0:0:0:1 -> the second (3-group) run is collapsed.
    auto p = make_v6({0x20, 0x01, 0, 0, 0, 0, 0, 0x01,
                      0,    0,    0, 0, 0, 0, 0, 0x01});
    LT_CHECK_EQ(p.to_string(), std::string{"2001:0:0:1::1"});
LT_END_AUTO_TEST(rfc5952_longest_run_wins)

// §4.2.3 tie-break: equal-length runs, first occurrence wins.
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, rfc5952_first_occurrence_tie_break)
    // Groups: 1:0:0:1:0:0:1:1 -> the first (2-group) run is collapsed.
    auto p = make_v6({0, 0x01, 0, 0, 0, 0, 0, 0x01,
                      0,    0, 0, 0, 0, 0x01, 0, 0x01});
    LT_CHECK_EQ(p.to_string(), std::string{"1::1:0:0:1:1"});
LT_END_AUTO_TEST(rfc5952_first_occurrence_tie_break)

// §5: ::ffff:0:0/96 (IPv4-mapped) renders the last 32 bits as dotted-quad.
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, rfc5952_ipv4_mapped_dotted_quad)
    // ::ffff:192.0.2.1 (TEST-NET-1, RFC 5737)
    auto p = make_v6({0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0xff, 0xff, 192, 0, 2, 1});
    LT_CHECK_EQ(p.to_string(), std::string{"::ffff:192.0.2.1"});
LT_END_AUTO_TEST(rfc5952_ipv4_mapped_dotted_quad)

// Regression: IPv4 path stays unchanged (acceptance criterion).
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, ipv4_path_unchanged)
    auto p = make_v4(127, 0, 0, 1);
    LT_CHECK_EQ(p.to_string(), std::string{"127.0.0.1"});
LT_END_AUTO_TEST(ipv4_path_unchanged)

// Regression: unspec family returns empty string (unchanged).
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, unspec_returns_empty)
    peer_address p{};
    LT_CHECK_EQ(p.to_string(), std::string{});
LT_END_AUTO_TEST(unspec_returns_empty)

// §4.2.2: collapse at the trailing edge.
LT_BEGIN_AUTO_TEST(peer_address_to_string_suite, trailing_collapse)
    // Groups: 2001:db8:1:0:0:0:0:0 -> "2001:db8:1::"
    auto p = make_v6({0x20, 0x01, 0x0d, 0xb8, 0, 0x01, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0});
    LT_CHECK_EQ(p.to_string(), std::string{"2001:db8:1::"});
LT_END_AUTO_TEST(trailing_collapse)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
