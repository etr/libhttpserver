/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <httpserver/detail/quic_transport_parameters.hpp>
#include <httpserver/detail/quic_varint.hpp>
#include "./quic_codec_test_support.hpp"
LT_BEGIN_SUITE(parameter_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(parameter_suite)
namespace {
bool equal_optional_bytes(const std::optional<std::span<const std::byte>>& a, const std::optional<std::span<const std::byte>>& b) {
    return a.has_value() == b.has_value() && (!a || std::equal(a->begin(), a->end(), b->begin(), b->end()));
}
}  // namespace
LT_BEGIN_AUTO_TEST(parameter_suite, fixed_client_and_server_blocks_defaults_and_serialization)
    auto wire = octets({1, 1, 10, 3, 2, 0x44, 0xb0, 4, 1, 30, 5, 1, 10, 6, 1, 11, 7, 1, 12, 8, 1, 5, 9, 1, 6, 10, 1, 4, 11, 1, 26, 12, 0, 14, 1, 3, 15, 2, 0xab, 0xcd});
    for (auto sender : {hd::quic_endpoint_role::client, hd::quic_endpoint_role::server}) {
        auto block = wire;
        if (sender == hd::quic_endpoint_role::server) {
            auto prefix = octets({0, 1, 0xab, 2, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
            block.insert(block.begin(), prefix.begin(), prefix.end());
            auto retry = octets({16, 1, 0xef});
            block.insert(block.end(), retry.begin(), retry.end());
        }
        auto parsed = hd::decode_quic_transport_parameters(block, sender);
        LT_ASSERT(parsed.code == hd::quic_codec_code::ok);
        const auto& p = parsed.value;
        LT_CHECK_EQ(parsed.consumed, block.size());
        LT_CHECK_EQ(p.max_idle_timeout, std::uint64_t{10});
        LT_CHECK_EQ(p.max_udp_payload_size, std::uint64_t{1200});
        LT_CHECK_EQ(p.initial_max_data, std::uint64_t{30});
        LT_CHECK_EQ(p.initial_max_stream_data_bidi_local, std::uint64_t{10});
        LT_CHECK_EQ(p.initial_max_stream_data_bidi_remote, std::uint64_t{11});
        LT_CHECK_EQ(p.initial_max_stream_data_uni, std::uint64_t{12});
        LT_CHECK_EQ(p.initial_max_streams_bidi, std::uint64_t{5});
        LT_CHECK_EQ(p.initial_max_streams_uni, std::uint64_t{6});
        LT_CHECK_EQ(p.ack_delay_exponent, std::uint64_t{4});
        LT_CHECK_EQ(p.max_ack_delay, std::uint64_t{26});
        LT_CHECK_EQ(p.active_connection_id_limit, std::uint64_t{3});
        LT_CHECK(p.disable_active_migration);
        LT_ASSERT(p.initial_source_cid.has_value());
        LT_CHECK_EQ(p.initial_source_cid->size(), std::size_t{2});
        std::vector<std::byte> out(block.size() + 10, std::byte{0x55});
        auto written = hd::encode_quic_transport_parameters(p, sender, out);
        LT_ASSERT(written.code == hd::quic_codec_code::ok);
        // Independent canonical bytes put ID 1 between the server's IDs 0 and 2.
        const auto canonical = sender == hd::quic_endpoint_role::client
                                   ? octets({1, 1, 10, 3, 2, 0x44, 0xb0, 4, 1, 30, 5, 1, 10, 6, 1, 11, 7, 1, 12, 8, 1, 5, 9, 1, 6, 10, 1, 4, 11, 1, 26, 12, 0, 14, 1, 3, 15, 2, 0xab, 0xcd})
                                   : octets({0, 1, 0xab, 1, 1, 10, 2, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 3, 2, 0x44, 0xb0, 4, 1, 30,
                                             5, 1, 10, 6, 1, 11, 7, 1, 12, 8, 1, 5, 9, 1, 6, 10, 1, 4, 11, 1, 26, 12, 0, 14, 1, 3, 15, 2, 0xab, 0xcd, 16, 1, 0xef});
        LT_CHECK_EQ(written.consumed, canonical.size());
        LT_CHECK(std::equal(out.begin(), out.begin() + written.consumed, canonical.begin(), canonical.end()));
        LT_CHECK(std::all_of(out.begin() + written.consumed, out.end(), [](auto b) { return b == std::byte{0x55}; }));
        auto reparsed = hd::decode_quic_transport_parameters(std::span(out).first(written.consumed), sender);
        LT_ASSERT(reparsed.code == hd::quic_codec_code::ok);
        const auto& round = reparsed.value;
        LT_CHECK_EQ(round.present, p.present);
        LT_CHECK_EQ(round.max_idle_timeout, p.max_idle_timeout);
        LT_CHECK_EQ(round.max_udp_payload_size, p.max_udp_payload_size);
        LT_CHECK_EQ(round.initial_max_data, std::uint64_t{30});
        LT_CHECK_EQ(round.initial_max_data, p.initial_max_data);
        LT_CHECK_EQ(round.initial_max_stream_data_bidi_local, p.initial_max_stream_data_bidi_local);
        LT_CHECK_EQ(round.initial_max_stream_data_bidi_remote, p.initial_max_stream_data_bidi_remote);
        LT_CHECK_EQ(round.initial_max_stream_data_uni, p.initial_max_stream_data_uni);
        LT_CHECK_EQ(round.initial_max_streams_bidi, p.initial_max_streams_bidi);
        LT_CHECK_EQ(round.initial_max_streams_uni, p.initial_max_streams_uni);
        LT_CHECK_EQ(round.ack_delay_exponent, p.ack_delay_exponent);
        LT_CHECK_EQ(round.max_ack_delay, p.max_ack_delay);
        LT_CHECK_EQ(round.active_connection_id_limit, p.active_connection_id_limit);
        LT_CHECK(round.disable_active_migration == p.disable_active_migration);
        LT_CHECK(equal_optional_bytes(round.initial_source_cid, p.initial_source_cid));
        LT_CHECK(equal_optional_bytes(round.original_destination_cid, p.original_destination_cid));
        LT_CHECK(equal_optional_bytes(round.retry_source_cid, p.retry_source_cid));
        LT_CHECK(equal_optional_bytes(round.stateless_reset_token, p.stateless_reset_token));
        LT_CHECK(round.preferred_address.has_value() == p.preferred_address.has_value());
        const auto original = out;
        LT_CHECK(hd::encode_quic_transport_parameters(p, sender, std::span(out).first(written.consumed - 1)).code == hd::quic_codec_code::no_space);
        LT_CHECK(out == original);
    }
    auto defaults = hd::decode_quic_transport_parameters({}, hd::quic_endpoint_role::client);
    LT_ASSERT(defaults.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(defaults.value.max_udp_payload_size, std::uint64_t{65527});
    LT_CHECK_EQ(defaults.value.ack_delay_exponent, std::uint64_t{3});
    LT_CHECK_EQ(defaults.value.max_ack_delay, std::uint64_t{25});
    LT_CHECK_EQ(defaults.value.active_connection_id_limit, std::uint64_t{2});
    LT_CHECK_EQ(defaults.value.initial_max_streams_uni, std::uint64_t{0});
    // Serialization uses ascending IDs, independent of receive order.
    auto reordered = octets({15, 0, 1, 1, 10});
    auto p = hd::decode_quic_transport_parameters(reordered, hd::quic_endpoint_role::client);
    std::array<std::byte, 5> out{};
    LT_CHECK(hd::encode_quic_transport_parameters(p.value, hd::quic_endpoint_role::client, out).code == hd::quic_codec_code::ok);
    LT_CHECK(std::equal(out.begin(), out.end(), octets({1, 1, 10, 15, 0}).begin()));
LT_END_AUTO_TEST(fixed_client_and_server_blocks_defaults_and_serialization)
LT_BEGIN_AUTO_TEST(parameter_suite, strict_tuple_lengths_duplicates_and_policy_bounds)
    for (auto bad : {octets({1}), octets({1, 1}), octets({1, 0}), octets({1, 2, 0, 0}), octets({1, 1, 0x40}), octets({1, 63, 0}), octets({1, 1, 0, 1, 1, 1}), octets({27, 0, 0x40, 27, 0}),
                     octets({12, 1, 0}), octets({2, 15, 0}), octets({15, 21}), octets({16, 21}), octets({0, 21})}) {
        auto r = hd::decode_quic_transport_parameters(bad, hd::quic_endpoint_role::server);
        LT_CHECK(r.code != hd::quic_codec_code::ok);
        LT_CHECK_EQ(r.consumed, std::size_t{0});
        LT_CHECK_EQ(r.value.present, std::uint32_t{0});
    }
    auto unknown = octets({27, 2, 1, 2, 15, 0});
    LT_CHECK(hd::decode_quic_transport_parameters(unknown, hd::quic_endpoint_role::client).code == hd::quic_codec_code::ok);
    LT_CHECK(hd::decode_quic_transport_parameters(unknown, hd::quic_endpoint_role::client, {100, 100, 1, 100}).code == hd::quic_codec_code::limit_exceeded);
    LT_CHECK(hd::decode_quic_transport_parameters(unknown, hd::quic_endpoint_role::client, {1, 100, 100, 100}).code == hd::quic_codec_code::limit_exceeded);
    std::vector<std::byte> many;
    for (unsigned i = 0; i <= 256; ++i) {
        std::array<std::byte, 8> id{};
        auto r = hd::encode_quic_varint(100 + i, id);
        many.insert(many.end(), id.begin(), id.begin() + r.consumed);
        many.push_back(std::byte{0});
    }
    LT_CHECK(hd::decode_quic_transport_parameters(many, hd::quic_endpoint_role::client).code == hd::quic_codec_code::limit_exceeded);
    auto huge = octets({27, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    LT_CHECK(hd::decode_quic_transport_parameters(huge, hd::quic_endpoint_role::client).code == hd::quic_codec_code::truncated);
LT_END_AUTO_TEST(strict_tuple_lengths_duplicates_and_policy_bounds)
LT_BEGIN_AUTO_TEST(parameter_suite, numeric_limits_and_sender_restrictions)
    struct boundary {
        unsigned id;
        std::uint64_t valid, invalid;
    };
    for (const auto b : {boundary{3, 1200, 1199}, boundary{8, std::uint64_t{1} << 60, (std::uint64_t{1} << 60) + 1}, boundary{9, std::uint64_t{1} << 60, (std::uint64_t{1} << 60) + 1},
                         boundary{10, 20, 21}, boundary{11, 16383, 16384}, boundary{14, 2, 1}}) {
        for (bool valid : {true, false}) {
            std::array<std::byte, 8> encoded{};
            auto r = hd::encode_quic_varint(valid ? b.valid : b.invalid, encoded);
            auto wire = octets({b.id, static_cast<unsigned>(r.consumed)});
            wire.insert(wire.end(), encoded.begin(), encoded.begin() + r.consumed);
            LT_CHECK((hd::decode_quic_transport_parameters(wire, hd::quic_endpoint_role::client).code == hd::quic_codec_code::ok) == valid);
        }
    }
    for (auto wire : {octets({0, 0}), octets({16, 0}), octets({2, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), octets({13, 0})}) {
        LT_CHECK(hd::decode_quic_transport_parameters(wire, hd::quic_endpoint_role::client).code == hd::quic_codec_code::malformed);
    }
    hd::quic_transport_parameters invalid;
    invalid.max_udp_payload_size = 1199;
    std::array<std::byte, 100> out{};
    LT_CHECK(hd::encode_quic_transport_parameters(invalid, hd::quic_endpoint_role::client, out).code == hd::quic_codec_code::malformed);
LT_END_AUTO_TEST(numeric_limits_and_sender_restrictions)
LT_BEGIN_AUTO_TEST(parameter_suite, preferred_address_and_handshake_cid_authentication)
    auto address = octets({13, 42, 127, 0, 0, 1, 0x01, 0xbb});
    address.resize(26);
    auto tail = octets({1, 0xab, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
    address.insert(address.end(), tail.begin(), tail.end());
    auto parsed = hd::decode_quic_transport_parameters(address, hd::quic_endpoint_role::server);
    LT_ASSERT(parsed.code == hd::quic_codec_code::ok);
    LT_ASSERT(parsed.value.preferred_address.has_value());
    LT_CHECK_EQ(parsed.value.preferred_address->ipv4_port, std::uint16_t{443});
    LT_CHECK_EQ(parsed.value.preferred_address->cid.size(), std::size_t{1});
    std::vector<std::byte> encoded_address(address.size());
    auto written = hd::encode_quic_transport_parameters(parsed.value, hd::quic_endpoint_role::server, encoded_address);
    LT_CHECK(written.code == hd::quic_codec_code::ok);
    LT_CHECK(encoded_address == address);
    for (std::size_t n = 1; n < address.size(); ++n)
        LT_CHECK(hd::decode_quic_transport_parameters(std::span(address).first(n), hd::quic_endpoint_role::server).code != hd::quic_codec_code::ok);
    auto bad = address;
    bad[26] = std::byte{0};
    LT_CHECK(hd::decode_quic_transport_parameters(bad, hd::quic_endpoint_role::server).code == hd::quic_codec_code::malformed);
    bad[26] = std::byte{21};
    LT_CHECK(hd::decode_quic_transport_parameters(bad, hd::quic_endpoint_role::server).code == hd::quic_codec_code::malformed);
    auto cids = octets({0, 1, 0xab, 15, 1, 0xcd, 16, 1, 0xef});
    auto p = hd::decode_quic_transport_parameters(cids, hd::quic_endpoint_role::server);
    LT_ASSERT(p.code == hd::quic_codec_code::ok);
    hd::quic_parameter_cid_context context{std::span(cids).subspan(5, 1), std::span(cids).subspan(2, 1), std::span(cids).subspan(8, 1)};
    LT_CHECK(hd::validate_quic_transport_parameters(p.value, hd::quic_endpoint_role::server, context) == hd::quic_codec_code::ok);
    context.retry_source = {};
    LT_CHECK(hd::validate_quic_transport_parameters(p.value, hd::quic_endpoint_role::server, context) == hd::quic_codec_code::malformed);
    context.retry_source = std::span(cids).subspan(8, 1);
    context.initial_source = {};
    LT_CHECK(hd::validate_quic_transport_parameters(p.value, hd::quic_endpoint_role::server, context) == hd::quic_codec_code::malformed);
    LT_CHECK(hd::validate_quic_transport_parameters({}, hd::quic_endpoint_role::client, {}) == hd::quic_codec_code::malformed);
LT_END_AUTO_TEST(preferred_address_and_handshake_cid_authentication)
LT_BEGIN_AUTO_TEST(parameter_suite, zero_length_cid_presence_and_unknown_count_boundary)
    const auto empty_cid = octets({15, 0});
    auto p = hd::decode_quic_transport_parameters(empty_cid, hd::quic_endpoint_role::client);
    LT_CHECK(hd::validate_quic_transport_parameters(p.value, hd::quic_endpoint_role::client, {}) == hd::quic_codec_code::ok);
    LT_CHECK(hd::validate_quic_transport_parameters(p.value, hd::quic_endpoint_role::server, {}) == hd::quic_codec_code::malformed);
    std::vector<std::byte> many;
    for (unsigned i = 0; i < 256; ++i) {
        std::array<std::byte, 8> id{};
        const auto written = hd::encode_quic_varint(100 + i, id);
        many.insert(many.end(), id.begin(), id.begin() + written.consumed);
        many.push_back(std::byte{0});
    }
    auto result = hd::decode_quic_transport_parameters(many, hd::quic_endpoint_role::client);
    LT_CHECK(result.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(result.consumed, many.size());
    auto integer_duplicate = octets({1, 1, 0, 0x40, 1, 1, 0});
    LT_CHECK(hd::decode_quic_transport_parameters(integer_duplicate, hd::quic_endpoint_role::client).code == hd::quic_codec_code::malformed);
LT_END_AUTO_TEST(zero_length_cid_presence_and_unknown_count_boundary)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
