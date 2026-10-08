/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <httpserver/detail/quic_frame.hpp>
#include <httpserver/detail/quic_varint.hpp>
#include "./quic_codec_test_support.hpp"
LT_BEGIN_SUITE(frame_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(frame_suite)
LT_BEGIN_AUTO_TEST(frame_suite, independent_vectors_for_every_core_frame_and_stream_flag)
    const std::vector<std::vector<std::byte>> vectors{octets({0, 0, 0}),
                                                      octets({1}),
                                                      octets({2, 10, 3, 1, 2, 1, 1}),
                                                      octets({3, 10, 3, 1, 2, 1, 1, 1, 2, 3}),
                                                      octets({4, 0, 5, 6}),
                                                      octets({5, 0, 7}),
                                                      octets({6, 4, 2, 0xab, 0xcd}),
                                                      octets({7, 1, 0xcc}),
                                                      octets({8, 0, 0xcc}),
                                                      octets({9, 0, 0xcc}),
                                                      octets({10, 0, 1, 0xcc}),
                                                      octets({11, 0, 1, 0xcc}),
                                                      octets({12, 0, 2, 0xcc}),
                                                      octets({13, 0, 2, 0xcc}),
                                                      octets({14, 0, 2, 1, 0xcc}),
                                                      octets({15, 0, 2, 1, 0xcc}),
                                                      octets({16, 9}),
                                                      octets({17, 0, 9}),
                                                      octets({18, 9}),
                                                      octets({19, 9}),
                                                      octets({20, 9}),
                                                      octets({21, 0, 9}),
                                                      octets({22, 9}),
                                                      octets({23, 9}),
                                                      octets({24, 4, 3, 1, 0xac, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}),
                                                      octets({25, 3}),
                                                      octets({26, 1, 2, 3, 4, 5, 6, 7, 8}),
                                                      octets({27, 8, 7, 6, 5, 4, 3, 2, 1}),
                                                      octets({28, 1, 8, 2, 0xab, 0xcd}),
                                                      octets({29, 1, 2, 0xab, 0xcd}),
                                                      octets({30})};
    for (const auto& wire : vectors) {
        hd::quic_frame_cursor cursor;
        const auto parsed = hd::next_quic_frame(wire, cursor);
        LT_ASSERT(parsed.code == hd::quic_codec_code::ok);
        LT_CHECK_EQ(parsed.consumed, wire.size());
        LT_CHECK_EQ(cursor.offset, wire.size());
        std::vector<std::byte> out(wire.size() + 1, std::byte{0x55});
        const auto encoded = hd::encode_quic_frame(parsed.value, out);
        LT_CHECK(encoded.code == hd::quic_codec_code::ok);
        LT_CHECK_EQ(encoded.consumed, wire.size());
        LT_CHECK(std::equal(wire.begin(), wire.end(), out.begin()));
        LT_CHECK(out.back() == std::byte{0x55});
        out.assign(wire.size(), std::byte{0x55});
        LT_CHECK(hd::encode_quic_frame(parsed.value, std::span(out).first(wire.size() - 1)).code == hd::quic_codec_code::no_space);
        LT_CHECK(std::all_of(out.begin(), out.end(), [](auto b) { return b == std::byte{0x55}; }));
        const auto type = std::to_integer<unsigned>(wire[0]);
        if (auto* f = std::get_if<hd::quic_stream_frame>(&parsed.value)) {
            LT_CHECK_EQ(f->stream, std::uint64_t{0});
            LT_CHECK_EQ(f->offset, (type & 4) ? std::uint64_t{2} : std::uint64_t{0});
            LT_CHECK(f->fin == static_cast<bool>(type & 1) && f->has_length == static_cast<bool>(type & 2) && f->has_offset == static_cast<bool>(type & 4));
            LT_ASSERT(f->data.size() == 1);
            LT_CHECK(f->data[0] == std::byte{0xcc});
        }
        if (auto* f = std::get_if<hd::quic_reset_stream_frame>(&parsed.value)) {
            LT_CHECK_EQ(f->stream, std::uint64_t{0});
            LT_CHECK_EQ(f->error, std::uint64_t{5});
            LT_CHECK_EQ(f->final_size, std::uint64_t{6});
        }
        if (auto* f = std::get_if<hd::quic_stop_sending_frame>(&parsed.value)) LT_CHECK_EQ(f->error, std::uint64_t{7});
        if (auto* f = std::get_if<hd::quic_crypto_frame>(&parsed.value)) {
            LT_CHECK_EQ(f->offset, std::uint64_t{4});
            LT_CHECK_EQ(f->data.size(), std::size_t{2});
        }
        if (auto* f = std::get_if<hd::quic_flow_frame>(&parsed.value)) {
            LT_CHECK(static_cast<unsigned>(f->kind) == type);
            LT_CHECK_EQ(f->limit, std::uint64_t{9});
        }
        if (auto* f = std::get_if<hd::quic_new_connection_id_frame>(&parsed.value)) {
            LT_CHECK_EQ(f->sequence, std::uint64_t{4});
            LT_CHECK_EQ(f->retire_prior_to, std::uint64_t{3});
            LT_ASSERT(f->cid.size() == 1 && f->reset_token.size() == 16);
            LT_CHECK(f->cid[0] == std::byte{0xac});
            LT_CHECK(f->reset_token.back() == std::byte{15});
        }
        if (auto* f = std::get_if<hd::quic_retire_connection_id_frame>(&parsed.value)) LT_CHECK_EQ(f->sequence, std::uint64_t{3});
        if (auto* f = std::get_if<hd::quic_path_frame>(&parsed.value)) LT_CHECK(f->response == (type == 27));
        if (auto* f = std::get_if<hd::quic_close_frame>(&parsed.value)) {
            LT_CHECK_EQ(f->error, std::uint64_t{1});
            LT_CHECK(f->application == (type == 29));
            LT_CHECK_EQ(f->frame_type, (type == 29) ? std::uint64_t{0} : std::uint64_t{8});
            LT_CHECK_EQ(f->reason.size(), std::size_t{2});
        }
        // No-length STREAM, PADDING and empty-data STREAM prefixes can be valid.
        if (type > 1 && (type < 8 || type > 15)) {
            for (std::size_t n = 0; n < wire.size(); ++n) {
                hd::quic_frame_cursor c;
                LT_CHECK(hd::next_quic_frame(std::span(wire).first(n), c).code != hd::quic_codec_code::ok);
                LT_CHECK_EQ(c.offset, std::size_t{0});
            }
        }
    }
LT_END_AUTO_TEST(independent_vectors_for_every_core_frame_and_stream_flag)
LT_BEGIN_AUTO_TEST(frame_suite, ack_ranges_arithmetic_and_ecn)
    auto wire = octets({3, 10, 3, 1, 2, 1, 1, 1, 2, 3});
    hd::quic_frame_cursor c;
    auto r = hd::next_quic_frame(wire, c);
    LT_ASSERT(r.code == hd::quic_codec_code::ok);
    auto ack = std::get<hd::quic_ack_frame>(r.value);
    hd::quic_ack_cursor a;
    auto range = hd::next_quic_ack_range(ack, a);
    LT_CHECK_EQ(range.value.smallest, std::uint64_t{8});
    LT_CHECK_EQ(range.value.largest, std::uint64_t{10});
    range = hd::next_quic_ack_range(ack, a);
    LT_CHECK_EQ(range.value.smallest, std::uint64_t{4});
    LT_CHECK_EQ(range.value.largest, std::uint64_t{5});
    LT_CHECK(hd::next_quic_ack_range(ack, a).code == hd::quic_codec_code::truncated);
    const std::array<hd::quic_ack_range, 2> ranges{{{8, 10}, {4, 5}}};
    std::array<std::byte, 20> out{};
    auto encoded = hd::encode_quic_ack(ranges, 3, std::array<std::uint64_t, 3>{1, 2, 3}, out);
    LT_CHECK(encoded.code == hd::quic_codec_code::ok);
    LT_CHECK(std::equal(wire.begin(), wire.end(), out.begin()));
    const std::array<hd::quic_ack_range, 2> invalid{{{8, 10}, {7, 7}}};
    out.fill(std::byte{0x55});
    LT_CHECK(hd::encode_quic_ack(invalid, 0, {}, out).code == hd::quic_codec_code::malformed);
    LT_CHECK(out[0] == std::byte{0x55});
    for (auto bad : {octets({2, 1, 0, 0, 2}), octets({2, 1, 0, 1, 0, 0, 0}), octets({2, 10, 0, 1, 2, 1, 6}), octets({2, 1, 0, 63, 0})}) {
        hd::quic_frame_cursor cursor;
        LT_CHECK(hd::next_quic_frame(bad, cursor).code != hd::quic_codec_code::ok);
        LT_CHECK_EQ(cursor.count, std::size_t{0});
    }
    c = {};
    LT_CHECK(hd::next_quic_frame(wire, c, {}, {100, 100, 100, 0}).code == hd::quic_codec_code::limit_exceeded);
LT_END_AUTO_TEST(ack_ranges_arithmetic_and_ecn)
LT_BEGIN_AUTO_TEST(frame_suite, malformed_shapes_overflows_and_work_limits)
    for (auto bad : {octets({0x40, 1}), octets({0x1f}), octets({7, 0}), octets({24, 1, 2, 1}), octets({24, 0, 0, 0}), octets({24, 0, 0, 21}), octets({6, 0, 63, 0}), octets({10, 0, 63, 0}),
                     octets({28, 0, 0, 63}), octets({18, 0xd0, 0, 0, 0, 0, 0, 0, 1})}) {
        hd::quic_frame_cursor c;
        auto r = hd::next_quic_frame(bad, c);
        LT_CHECK(r.code != hd::quic_codec_code::ok);
        LT_CHECK_EQ(r.consumed, std::size_t{0});
    }
    for (unsigned type : {6, 14}) {
        auto wire = octets({type});
        if (type == 14) wire.push_back(std::byte{0});
        std::array<std::byte, 8> maximum{};
        hd::encode_quic_varint(hd::k_quic_max_integer, maximum);
        wire.insert(wire.end(), maximum.begin(), maximum.end());
        wire.push_back(std::byte{1});
        wire.push_back(std::byte{0});
        hd::quic_frame_cursor c;
        LT_CHECK(hd::next_quic_frame(wire, c).code == hd::quic_codec_code::malformed);
    }
    auto sequence = octets({0, 0, 1, 10, 0, 1, 0xff, 1});
    hd::quic_frame_cursor c;
    LT_CHECK_EQ(hd::next_quic_frame(sequence, c).consumed, std::size_t{2});
    LT_CHECK_EQ(hd::next_quic_frame(sequence, c).consumed, std::size_t{1});
    LT_CHECK_EQ(hd::next_quic_frame(sequence, c).consumed, std::size_t{4});
    LT_CHECK(hd::next_quic_frame(sequence, c, {}, {100, 3, 100, 100}).code == hd::quic_codec_code::limit_exceeded);
    LT_CHECK(hd::next_quic_frame(sequence, c, {}, {1, 100, 100, 100}).code == hd::quic_codec_code::limit_exceeded);
    const auto token = octets({0xff});
    std::array<std::byte, 30> out{};
    LT_CHECK(hd::encode_quic_frame(hd::quic_padding_frame{0}, out).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::encode_quic_frame(hd::quic_path_frame{false, token}, out).code == hd::quic_codec_code::malformed);
    LT_CHECK(hd::encode_quic_frame(hd::quic_stream_frame{0, 1, token, false, false, true}, out).code == hd::quic_codec_code::malformed);
LT_END_AUTO_TEST(malformed_shapes_overflows_and_work_limits)
LT_BEGIN_AUTO_TEST(frame_suite, placement_and_unidirectional_sender_rules)
    for (auto packet : {hd::quic_packet_kind::initial, hd::quic_packet_kind::handshake, hd::quic_packet_kind::zero_rtt, hd::quic_packet_kind::one_rtt, hd::quic_packet_kind::retry,
                        hd::quic_packet_kind::version_negotiation}) {
        for (auto type : {0, 1, 2, 6, 7, 10, 16, 27, 28, 29, 30}) {
            std::vector<std::byte> wire;
            switch (type) {
                case 2:
                    wire = octets({2, 0, 0, 0, 0});
                    break;
                case 6:
                    wire = octets({6, 0, 0});
                    break;
                case 7:
                    wire = octets({7, 1, 0});
                    break;
                case 10:
                    wire = octets({10, 0, 0});
                    break;
                case 16:
                    wire = octets({16, 0});
                    break;
                case 27:
                    wire = octets({27, 0, 0, 0, 0, 0, 0, 0, 0});
                    break;
                case 28:
                    wire = octets({28, 0, 0, 0});
                    break;
                case 29:
                    wire = octets({29, 0, 0});
                    break;
                default:
                    wire = octets({static_cast<unsigned>(type)});
            }
            const bool initial_space = packet == hd::quic_packet_kind::initial || packet == hd::quic_packet_kind::handshake;
            bool allowed = packet == hd::quic_packet_kind::one_rtt;
            if (initial_space) allowed = type == 0 || type == 1 || type == 2 || type == 6 || type == 28;
            if (packet == hd::quic_packet_kind::zero_rtt) allowed = type == 0 || type == 1 || type == 10 || type == 16 || type == 28 || type == 29;
            hd::quic_frame_cursor c;
            LT_CHECK((hd::next_quic_frame(wire, c, {packet, hd::quic_endpoint_role::server}).code == hd::quic_codec_code::ok) == allowed);
        }
    }
    for (auto wire : {octets({7, 1, 0}), octets({30}), octets({10, 3, 0}), octets({4, 3, 0, 0}), octets({21, 3, 0}), octets({5, 2, 0}), octets({17, 2, 0})}) {
        hd::quic_frame_cursor c;
        LT_CHECK(hd::next_quic_frame(wire, c, {hd::quic_packet_kind::one_rtt, hd::quic_endpoint_role::client}).code == hd::quic_codec_code::malformed);
    }
LT_END_AUTO_TEST(placement_and_unidirectional_sender_rules)
LT_BEGIN_AUTO_TEST(frame_suite, ack_iterator_reports_each_encoded_pair_consumption)
    const auto wire = octets({2, 20, 0, 2, 0, 0, 0, 0, 0});
    hd::quic_frame_cursor c;
    const auto frame = hd::next_quic_frame(wire, c);
    LT_ASSERT(frame.code == hd::quic_codec_code::ok);
    const auto ack = std::get<hd::quic_ack_frame>(frame.value);
    hd::quic_ack_cursor ranges;
    const auto first = hd::next_quic_ack_range(ack, ranges);
    const auto second = hd::next_quic_ack_range(ack, ranges);
    const auto third = hd::next_quic_ack_range(ack, ranges);
    LT_CHECK_EQ(first.consumed, std::size_t{0});
    LT_CHECK_EQ(second.consumed, std::size_t{2});
    LT_CHECK_EQ(third.consumed, std::size_t{2});
    LT_CHECK_EQ(ranges.offset, std::size_t{4});
LT_END_AUTO_TEST(ack_iterator_reports_each_encoded_pair_consumption)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
