/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <httpserver/detail/quic_varint.hpp>
#include "./quic_codec_test_support.hpp"
LT_BEGIN_SUITE(varint_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(varint_suite)
LT_BEGIN_AUTO_TEST(varint_suite, rfc_octets_and_all_truncated_prefixes)
    struct example {
        std::uint64_t value;
        std::vector<std::byte> wire;
    };
    for (const auto& v : {example{37, octets({0x25})}, example{37, octets({0x40, 0x25})}, example{15293, octets({0x7b, 0xbd})}, example{494878333, octets({0x9d, 0x7f, 0x3e, 0x7d})},
                          example{151288809941952652ULL, octets({0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c})}}) {
        const auto r = hd::decode_quic_varint(v.wire);
        LT_CHECK(r.code == hd::quic_codec_code::ok);
        LT_CHECK_EQ(r.value, v.value);
        LT_CHECK_EQ(r.consumed, v.wire.size());
        for (std::size_t i = 0; i < v.wire.size(); ++i) {
            const auto bad = hd::decode_quic_varint(std::span(v.wire).first(i));
            LT_CHECK(bad.code == hd::quic_codec_code::truncated);
            LT_CHECK_EQ(bad.consumed, std::size_t{0});
        }
        std::array<std::byte, 9> output{};
        auto written = hd::encode_quic_varint(v.value, output, v.wire.size());
        LT_CHECK(written.code == hd::quic_codec_code::ok);
        LT_CHECK(std::equal(v.wire.begin(), v.wire.end(), output.begin()));
        output.fill(std::byte{0x55});
        LT_CHECK(hd::encode_quic_varint(v.value, std::span(output).first(v.wire.size() - 1), v.wire.size()).code == hd::quic_codec_code::no_space);
        LT_CHECK(std::all_of(output.begin(), output.end(), [](auto b) { return b == std::byte{0x55}; }));
    }
LT_END_AUTO_TEST(rfc_octets_and_all_truncated_prefixes)
LT_BEGIN_AUTO_TEST(varint_suite, width_transitions_and_sentinels)
    const std::array<std::uint64_t, 10> values{0, 63, 64, 16383, 16384, 1073741823, 1073741824, hd::k_quic_max_integer - 1, hd::k_quic_max_integer, 37};
    const std::array<std::size_t, 10> widths{1, 1, 2, 2, 4, 4, 8, 8, 8, 1};
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::array<std::byte, 9> wire{};
        wire.fill(std::byte{0xab});
        LT_CHECK_EQ(hd::quic_varint_width(values[i]), widths[i]);
        const auto result = hd::encode_quic_varint(values[i], wire);
        LT_CHECK_EQ(result.consumed, widths[i]);
        LT_CHECK_EQ(hd::decode_quic_varint(wire).value, values[i]);
        LT_CHECK_EQ(hd::decode_quic_varint(wire).consumed, widths[i]);
        LT_CHECK(wire[widths[i]] == std::byte{0xab});
    }
    std::array<std::byte, 8> wire{};
    for (auto width : {3, 5, 6, 7, 9})
        LT_CHECK(hd::encode_quic_varint(0, wire, width).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::encode_quic_varint(64, wire, 1).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::encode_quic_varint(hd::k_quic_max_integer + 1, wire).code == hd::quic_codec_code::malformed);
LT_END_AUTO_TEST(width_transitions_and_sentinels)
LT_BEGIN_AUTO_TEST(varint_suite, packet_numbers_use_separate_truncation_and_context)
    LT_CHECK_EQ(hd::reconstruct_quic_packet_number(0x9b32, 2, 0xa82f30ea).value, std::uint64_t{0xa82f9b32});
    LT_CHECK_EQ(hd::reconstruct_quic_packet_number(0, 1, std::nullopt).value, std::uint64_t{0});
    LT_CHECK_EQ(hd::reconstruct_quic_packet_number(0, 1, 127).value, std::uint64_t{256});
    LT_CHECK_EQ(hd::reconstruct_quic_packet_number(255, 1, 127).value, std::uint64_t{255});
    LT_CHECK_EQ(hd::reconstruct_quic_packet_number(255, 1, 256).value, std::uint64_t{255});
    LT_CHECK_EQ(hd::reconstruct_quic_packet_number(255, 1, hd::k_quic_max_integer - 1).value, hd::k_quic_max_integer);
    LT_CHECK(hd::reconstruct_quic_packet_number(0, 0, {}).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::reconstruct_quic_packet_number(256, 1, {}).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::reconstruct_quic_packet_number(0, 1, hd::k_quic_max_integer + 1).code == hd::quic_codec_code::malformed);
    const auto expected = octets({0x12, 0x34, 0x56, 0x78});
    for (std::size_t n = 1; n <= 4; ++n) {
        std::array<std::byte, 4> out{};
        LT_CHECK(hd::encode_quic_packet_number(0x12345678, n, out).code == hd::quic_codec_code::ok);
        LT_CHECK(std::equal(expected.end() - n, expected.end(), out.begin()));
        LT_CHECK(hd::encode_quic_packet_number(3, n, std::span(out).first(n - 1)).code == hd::quic_codec_code::no_space);
    }
LT_END_AUTO_TEST(packet_numbers_use_separate_truncation_and_context)
LT_BEGIN_AUTO_TEST(varint_suite, fixed_transition_octets_and_reconstruction_windows_for_every_width)
    struct vector {
        std::uint64_t value;
        std::vector<std::byte> wire;
    };
    for (const auto& v : {vector{63, octets({0x3f})}, vector{64, octets({0x40, 0x40})}, vector{16383, octets({0x7f, 0xff})}, vector{16384, octets({0x80, 0, 0x40, 0})},
                          vector{1073741823, octets({0xbf, 0xff, 0xff, 0xff})}, vector{1073741824, octets({0xc0, 0, 0, 0, 0x40, 0, 0, 0})},
                          vector{hd::k_quic_max_integer, octets({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})}}) {
        std::array<std::byte, 8> encoded{};
        auto written = hd::encode_quic_varint(v.value, encoded);
        LT_CHECK_EQ(written.consumed, v.wire.size());
        LT_CHECK(std::equal(v.wire.begin(), v.wire.end(), encoded.begin()));
        LT_CHECK_EQ(hd::decode_quic_varint(v.wire).value, v.value);
    }
    for (std::size_t width = 1; width <= 4; ++width) {
        const auto window = std::uint64_t{1} << (width * 8);
        LT_CHECK_EQ(hd::reconstruct_quic_packet_number(0, width, window / 2 - 1).value, window);
        LT_CHECK_EQ(hd::reconstruct_quic_packet_number(window - 1, width, window).value, window - 1);
        LT_CHECK_EQ(hd::reconstruct_quic_packet_number(7, width, {}).value, std::uint64_t{7});
    }
    std::array<std::byte, 8> output{};
    output.fill(std::byte{0x55});
    for (auto width : {0, 5})
        LT_CHECK(hd::encode_quic_packet_number(1, width, output).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::encode_quic_packet_number(hd::k_quic_max_integer + 1, 1, output).code == hd::quic_codec_code::malformed);
    LT_CHECK(std::all_of(output.begin(), output.end(), [](auto b) { return b == std::byte{0x55}; }));
LT_END_AUTO_TEST(fixed_transition_octets_and_reconstruction_windows_for_every_width)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
