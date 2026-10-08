/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <httpserver/detail/quic_packet.hpp>
#include "./quic_codec_test_support.hpp"
LT_BEGIN_SUITE(packet_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(packet_suite)
LT_BEGIN_AUTO_TEST(packet_suite, fixed_envelopes_headers_and_transactional_writes)
    const auto dcid = octets({0xab, 0xcd}), scid = octets({0xef}), token = octets({0x11, 0x22});
    const auto payload = octets({0x99});
    const std::array<std::byte, 16> tag{};
    const auto versions = octets({0, 0, 0, 1, 0x0a, 0x0a, 0x0a, 0x0a});
    for (auto kind : {hd::quic_packet_kind::initial, hd::quic_packet_kind::handshake, hd::quic_packet_kind::zero_rtt, hd::quic_packet_kind::one_rtt, hd::quic_packet_kind::retry,
                      hd::quic_packet_kind::version_negotiation}) {
        const bool numbered = kind != hd::quic_packet_kind::retry && kind != hd::quic_packet_kind::version_negotiation;
        for (std::size_t width = 1; width <= (numbered ? 4U : 1U); ++width) {
            unsigned first = kind == hd::quic_packet_kind::one_rtt               ? 0x40
                             : kind == hd::quic_packet_kind::handshake           ? 0xe0
                             : kind == hd::quic_packet_kind::zero_rtt            ? 0xd0
                             : kind == hd::quic_packet_kind::retry               ? 0xf0
                             : kind == hd::quic_packet_kind::version_negotiation ? 0x80
                                                                                 : 0xc0;
            if (numbered) first += width - 1;
            auto expected = octets({first});
            if (kind != hd::quic_packet_kind::one_rtt) {
                auto prefix = octets({0, 0, 0, kind == hd::quic_packet_kind::version_negotiation ? 0U : 1U, 2, 0xab, 0xcd, 1, 0xef});
                expected.insert(expected.end(), prefix.begin(), prefix.end());
            } else {
                expected.insert(expected.end(), dcid.begin(), dcid.end());
            }
            if (kind == hd::quic_packet_kind::initial) {
                auto t = octets({2, 0x11, 0x22});
                expected.insert(expected.end(), t.begin(), t.end());
            }
            if (numbered && kind != hd::quic_packet_kind::one_rtt) expected.push_back(std::byte(width + 17));
            if (numbered) {
                for (std::size_t n = 1; n < width; ++n)
                    expected.push_back(std::byte{0});
                expected.push_back(std::byte{7});
                expected.push_back(std::byte{0x99});
            }
            if (kind == hd::quic_packet_kind::retry) expected.insert(expected.end(), token.begin(), token.end());
            if (kind == hd::quic_packet_kind::version_negotiation)
                expected.insert(expected.end(), versions.begin(), versions.end());
            else
                expected.insert(expected.end(), tag.begin(), tag.end());
            const auto parsed = hd::parse_quic_envelope(expected, 2);
            LT_ASSERT(parsed.code == hd::quic_codec_code::ok);
            LT_CHECK(parsed.value.kind == kind);
            LT_CHECK_EQ(parsed.consumed, expected.size());
            LT_CHECK_EQ(parsed.value.destination.size(), std::size_t{2});
            if (numbered) {
                auto pn = std::span(expected).subspan(parsed.value.packet_number_offset, width);
                const auto clear = hd::decode_quic_unprotected_header(parsed.value, {static_cast<std::uint8_t>(first), pn}, std::nullopt);
                LT_ASSERT(clear.code == hd::quic_codec_code::ok);
                LT_CHECK_EQ(clear.value.packet_number, std::uint64_t{7});
                LT_CHECK(hd::validate_quic_authenticated_header(clear.value) == hd::quic_codec_code::ok);
            }
            hd::quic_packet_write p;
            p.kind = kind;
            p.destination = dcid;
            p.source = kind == hd::quic_packet_kind::one_rtt ? std::span<const std::byte>{} : scid;
            if (kind == hd::quic_packet_kind::initial || kind == hd::quic_packet_kind::retry) p.token = token;
            if (numbered) {
                p.payload = payload;
                p.packet_number = 7;
                p.packet_number_width = width;
            }
            if (kind == hd::quic_packet_kind::version_negotiation)
                p.versions = versions;
            else
                p.tag = tag;
            std::vector<std::byte> output(expected.size() + 1, std::byte{0x55});
            auto encoded = hd::encode_quic_packet(p, output);
            LT_CHECK(encoded.code == hd::quic_codec_code::ok);
            LT_CHECK_EQ(encoded.consumed, expected.size());
            LT_CHECK(std::equal(expected.begin(), expected.end(), output.begin()));
            LT_CHECK(output.back() == std::byte{0x55});
            output.assign(expected.size(), std::byte{0x55});
            LT_CHECK(hd::encode_quic_packet(p, std::span(output).first(expected.size() - 1)).code == hd::quic_codec_code::no_space);
            LT_CHECK(std::all_of(output.begin(), output.end(), [](auto b) { return b == std::byte{0x55}; }));
            // Length-delimited packets require every byte advertised on wire.
            if (kind != hd::quic_packet_kind::one_rtt && kind != hd::quic_packet_kind::retry && kind != hd::quic_packet_kind::version_negotiation) {
                for (std::size_t n = 0; n < expected.size(); ++n)
                    LT_CHECK(hd::parse_quic_envelope(std::span(expected).first(n), 2).code != hd::quic_codec_code::ok);
            }
        }
    }
LT_END_AUTO_TEST(fixed_envelopes_headers_and_transactional_writes)
LT_BEGIN_AUTO_TEST(packet_suite, bounds_coalescing_and_protection_phase)
    auto initial = octets({0xcf, 0, 0, 0, 1, 0, 0, 0, 17, 5});
    initial.resize(26);
    auto coalesced = initial;
    coalesced.insert(coalesced.end(), initial.begin(), initial.end());
    auto r = hd::parse_quic_envelope(coalesced);
    LT_ASSERT(r.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(r.consumed, initial.size());
    LT_CHECK(hd::parse_quic_envelope(std::span(coalesced).subspan(r.consumed)).code == hd::quic_codec_code::ok);
    // Protected low bits advertise four bytes, supplied unmasked fields one.
    auto clear = hd::decode_quic_unprotected_header(r.value, {0xc0, std::span(initial).subspan(9, 1)}, {});
    LT_ASSERT(clear.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(clear.value.packet_number_width, std::size_t{1});
    clear = hd::decode_quic_unprotected_header(r.value, {0xcc, std::span(initial).subspan(9, 1)}, {});
    LT_CHECK(clear.code == hd::quic_codec_code::ok);
    LT_CHECK(hd::validate_quic_authenticated_header(clear.value) == hd::quic_codec_code::malformed);
    LT_CHECK(hd::decode_quic_unprotected_header(r.value, {0xc3, std::span(initial).subspan(9, 1)}, {}).code == hd::quic_codec_code::malformed);
    initial[4] = std::byte{2};
    LT_CHECK(hd::parse_quic_envelope(initial).code == hd::quic_codec_code::unsupported_version);
    initial[4] = std::byte{1};
    initial[0] = std::byte{0x80};
    LT_CHECK(hd::parse_quic_envelope(initial).code == hd::quic_codec_code::malformed);
    initial[0] = std::byte{0xc0};
    initial[5] = std::byte{21};
    LT_CHECK(hd::parse_quic_envelope(initial).code == hd::quic_codec_code::malformed);
    initial[5] = std::byte{0};
    initial[8] = std::byte{63};
    LT_CHECK(hd::parse_quic_envelope(initial).code == hd::quic_codec_code::truncated);
    initial[8] = std::byte{16};
    LT_CHECK(hd::parse_quic_envelope(initial).code == hd::quic_codec_code::malformed);
    auto vn = octets({0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
    LT_CHECK(hd::parse_quic_envelope(vn).code == hd::quic_codec_code::ok);
    vn.pop_back();
    LT_CHECK(hd::parse_quic_envelope(vn).code == hd::quic_codec_code::malformed);
    vn.resize(7);
    LT_CHECK(hd::parse_quic_envelope(vn).code == hd::quic_codec_code::malformed);
    auto retry = octets({0xf0, 0, 0, 0, 1, 0, 0});
    retry.resize(23);
    LT_CHECK(hd::parse_quic_envelope(retry).code == hd::quic_codec_code::malformed);
    retry.resize(22);
    LT_CHECK(hd::parse_quic_envelope(retry).code == hd::quic_codec_code::truncated);
    auto short_packet = octets({0x7f, 1, 2, 7});
    short_packet.resize(24);
    LT_CHECK(hd::parse_quic_envelope(short_packet).code == hd::quic_codec_code::malformed);
    r = hd::parse_quic_envelope(short_packet, 2);
    LT_ASSERT(r.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(r.consumed, short_packet.size());
    clear = hd::decode_quic_unprotected_header(r.value, {0x64, std::span(short_packet).subspan(3, 1)}, {});
    LT_CHECK(clear.value.key_phase && clear.value.spin);
    LT_CHECK(hd::parse_quic_envelope(short_packet, 21).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::parse_quic_envelope(short_packet, 2, {1, 1, 1, 1}).code == hd::quic_codec_code::limit_exceeded);
LT_END_AUTO_TEST(bounds_coalescing_and_protection_phase)
LT_BEGIN_AUTO_TEST(packet_suite, explicit_wider_lengths_exact_output_and_maximum_cid_views)
    auto wire = octets({0xc0, 0, 0, 0, 1, 0, 0, 0x40, 2, 0xab, 0xcd, 0x40, 17, 7});
    wire.resize(30);
    auto r = hd::parse_quic_envelope(wire);
    LT_ASSERT(r.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(r.value.packet_number_offset, std::size_t{13});
    LT_CHECK_EQ(r.value.token.size(), std::size_t{2});
    LT_CHECK(r.value.token.data() == wire.data() + 9);
    hd::quic_packet_write p;
    p.token = std::span(wire).subspan(9, 2);
    p.tag = std::span(wire).last(16);
    p.packet_number = 7;
    p.token_length_width = 2;
    p.length_width = 2;
    std::array<std::byte, 30> output{};
    LT_CHECK(hd::encode_quic_packet(p, output).code == hd::quic_codec_code::ok);
    LT_CHECK(std::equal(output.begin(), output.end(), wire.begin()));
    auto invalid = p;
    invalid.length_width = 3;
    output.fill(std::byte{0x55});
    LT_CHECK(hd::encode_quic_packet(invalid, output).code == hd::quic_codec_code::malformed);
    LT_CHECK(std::all_of(output.begin(), output.end(), [](auto b) { return b == std::byte{0x55}; }));
    auto max_cid = octets({0xe0, 0, 0, 0, 1, 20});
    max_cid.resize(26, std::byte{0xab});
    max_cid.push_back(std::byte{20});
    max_cid.resize(47, std::byte{0xcd});
    max_cid.push_back(std::byte{17});
    max_cid.resize(65);
    r = hd::parse_quic_envelope(max_cid);
    LT_ASSERT(r.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(r.value.destination.size(), std::size_t{20});
    LT_CHECK_EQ(r.value.source.size(), std::size_t{20});
    LT_CHECK(r.value.destination.data() == max_cid.data() + 6 && r.value.source.data() == max_cid.data() + 27);
    auto huge_token = octets({0xc0, 0, 0, 0, 1, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    LT_CHECK(hd::parse_quic_envelope(huge_token).code == hd::quic_codec_code::truncated);
LT_END_AUTO_TEST(explicit_wider_lengths_exact_output_and_maximum_cid_views)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
