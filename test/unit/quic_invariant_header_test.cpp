/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <vector>
#include <httpserver/detail/quic_invariant_header.hpp>
#include "./littletest.hpp"
namespace hd = httpserver::detail;
LT_BEGIN_SUITE(invariant_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(invariant_suite)
LT_BEGIN_AUTO_TEST(invariant_suite, long_header_bounds_and_binary_cids)
    std::array<std::byte, 10> packet{std::byte{0xc0}, {}, {}, {}, std::byte{1}, std::byte{2},
        std::byte{0xab}, {}, std::byte{1}, std::byte{0xcd}};
    for (std::size_t length = 0; length < packet.size(); ++length) {
        LT_CHECK(!hd::extract_quic_invariant_header(std::span(packet).first(length)));
    }
    auto parsed = hd::extract_quic_invariant_header(packet);
    LT_CHECK(parsed.has_value());
    if (parsed) {
        LT_CHECK_EQ(parsed->version, std::uint32_t{1});
        LT_CHECK_EQ(parsed->destination.size, std::size_t{2});
        LT_CHECK(parsed->destination.bytes[0] == std::byte{0xab});
        LT_CHECK(parsed->destination.bytes[1] == std::byte{0});
        LT_CHECK_EQ(parsed->source.size, std::size_t{1});
    }
    packet[5] = std::byte{21};
    LT_CHECK(!hd::extract_quic_invariant_header(packet));
LT_END_AUTO_TEST(long_header_bounds_and_binary_cids)
LT_BEGIN_AUTO_TEST(invariant_suite, short_header_requires_explicit_cid_length)
    std::array<std::byte, 4> packet{std::byte{0x40}, std::byte{1}, {}, std::byte{99}};
    LT_CHECK(!hd::extract_quic_invariant_header(packet));
    LT_CHECK(!hd::extract_quic_invariant_header(packet, 4));
    LT_CHECK(!hd::extract_quic_invariant_header(packet, 21));
    const auto parsed = hd::extract_quic_invariant_header(packet, 2);
    LT_CHECK(parsed.has_value());
    if (parsed) {
        LT_CHECK(!parsed->long_header);
        LT_CHECK_EQ(parsed->destination.size, std::size_t{2});
        LT_CHECK(parsed->destination.bytes[1] == std::byte{0});
    }
    packet[0] = std::byte{0};
    LT_CHECK(!hd::extract_quic_invariant_header(packet, 2));
LT_END_AUTO_TEST(short_header_requires_explicit_cid_length)
LT_BEGIN_AUTO_TEST(invariant_suite, maximum_cid_and_version_negotiation_bounds)
    std::vector<std::byte> packet(27, std::byte{0});
    packet[0] = std::byte{0x80};
    packet[5] = std::byte{20};
    for (std::size_t i = 0; i < 20; ++i) packet[6 + i] = std::byte(i);
    auto header = hd::extract_quic_invariant_header(packet);
    LT_CHECK(header.has_value());
    LT_CHECK_EQ(header->version, std::uint32_t{0});
    LT_CHECK_EQ(header->destination.size, std::size_t{20});
    LT_CHECK_EQ(header->source.size, std::size_t{0});
    LT_CHECK(!hd::extract_quic_invariant_header(std::span(packet).first(26)));
    packet[26] = std::byte{21};
    packet.resize(48);
    LT_CHECK(!hd::extract_quic_invariant_header(packet));
    packet[26] = std::byte{20};
    LT_CHECK(!hd::extract_quic_invariant_header(std::span(packet).first(46)));
    LT_CHECK(hd::extract_quic_invariant_header(packet).has_value());
LT_END_AUTO_TEST(maximum_cid_and_version_negotiation_bounds)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
