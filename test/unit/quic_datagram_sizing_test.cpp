/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <utility>
#include <httpserver/detail/quic_datagram_sizing.hpp>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
#include "../littletest.hpp"
namespace hd = httpserver::detail;
using space = hd::quic_pn_space;
namespace {
auto budget() { return httpserver::server::resource_budget::root({}); }
hd::quic_packet_write header(hd::quic_packet_kind kind, std::span<const std::byte> cid) {
    hd::quic_packet_write h;
    h.kind = kind;
    h.destination = cid;
    h.packet_number_width = 4;
    h.length_width = 2;
    return h;
}
}  // namespace
LT_BEGIN_SUITE(datagram_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(datagram_suite)
LT_BEGIN_AUTO_TEST(datagram_suite, initial_padding_applies_once_to_total_protected_datagram)
    std::array<std::byte, 8> cid{};
    std::array<hd::quic_datagram_packet, 2> packets{{{header(hd::quic_packet_kind::initial, cid), 100, true},
                                                 {header(hd::quic_packet_kind::handshake, cid), 100, true}}};
    auto p = hd::size_quic_datagram(packets, hd::quic_endpoint_role::server, 1200);
    LT_CHECK(p.code == hd::quic_codec_code::ok && p.total == 1200);
    LT_CHECK(p.payload[0] == 100 && p.payload[1] > 100 && p.wire[0] + p.wire[1] == 1200);
LT_END_AUTO_TEST(initial_padding_applies_once_to_total_protected_datagram)
LT_BEGIN_AUTO_TEST(datagram_suite, server_ack_only_initial_stays_small_but_client_initial_is_padded)
    std::array<std::byte, 20> cid{};
    std::array<std::byte, 70> token{};
    std::array<hd::quic_datagram_packet, 1> packets{{{header(hd::quic_packet_kind::initial, cid), 100, false}}};
    packets[0].header.source = cid;
    packets[0].header.token = token;
    packets[0].header.token_length_width = 2;
    auto server = hd::size_quic_datagram(packets, hd::quic_endpoint_role::server, 1200);
    auto client = hd::size_quic_datagram(packets, hd::quic_endpoint_role::client, 1200);
    LT_CHECK(server.code == hd::quic_codec_code::ok && server.total == 241);
    LT_CHECK(client.code == hd::quic_codec_code::ok && client.total == 1200 && client.padding[0]);
LT_END_AUTO_TEST(server_ack_only_initial_stays_small_but_client_initial_is_padded)
LT_BEGIN_AUTO_TEST(datagram_suite, rejects_oversized_total_mismatched_cids_and_short_header_before_long)
    std::array<std::byte, 8> cid{}, other{std::byte{1}};
    std::array<hd::quic_datagram_packet, 2> packets{{{header(hd::quic_packet_kind::handshake, cid), 700, true},
                                                 {header(hd::quic_packet_kind::one_rtt, cid), 700, true}}};
    LT_CHECK(hd::size_quic_datagram(packets, hd::quic_endpoint_role::server, 1200).code != hd::quic_codec_code::ok);
    packets[0].payload_bytes = packets[1].payload_bytes = 100;
    packets[1].header.destination = other;
    LT_CHECK(hd::size_quic_datagram(packets, hd::quic_endpoint_role::server, 1200).code != hd::quic_codec_code::ok);
    packets[1].header.destination = cid;
    std::swap(packets[0], packets[1]);
    LT_CHECK(hd::size_quic_datagram(packets, hd::quic_endpoint_role::server, 1200).code != hd::quic_codec_code::ok);
LT_END_AUTO_TEST(rejects_oversized_total_mismatched_cids_and_short_header_before_long)
LT_BEGIN_AUTO_TEST(datagram_suite, two_space_preparations_commit_or_abandon_as_one_datagram)
    hd::quic_recovery r({}, budget());
    hd::quic_transport_parameters params;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, params, params, 4, budget());
    std::array<std::byte, 1200> one{}, two{};
    LT_ASSERT(r.retain_crypto(space::initial, 0, std::span(one).first(10)));
    LT_ASSERT(r.retain_crypto(space::handshake, 0, std::span(two).first(10)));
    LT_CHECK(r.begin_datagram());
    if (!r.preparation_pending()) return;
    auto a = r.prepare_datagram_packet(space::initial, one, {}, {600, 40}, f);
    auto b = r.prepare_datagram_packet(space::handshake, two, {}, {600, 40}, f);
    LT_CHECK(a && b);
    if (!a || !b) return;
    LT_CHECK(r.activate_path().code == hd::quic_recovery_code::busy);
    LT_CHECK(r.reserve_packet(space::application).code == hd::quic_recovery_code::busy);
    std::array<hd::quic_datagram_emission, 2> actual{{{a.token, 600, true, true}, {b.token, 600, true, true}}};
    LT_ASSERT(r.check_datagram_emission(actual, {}));
    LT_ASSERT(r.abandon_datagram());
    LT_CHECK(r.bytes_in_flight() == 0 && !r.preparation_pending());
    LT_ASSERT(r.begin_datagram());
    auto aa = r.prepare_datagram_packet(space::initial, one, {}, {600, 40}, f);
    auto bb = r.prepare_datagram_packet(space::handshake, two, {}, {600, 40}, f);
    LT_ASSERT(aa && bb && aa.packet_number > a.packet_number && bb.packet_number > b.packet_number);
    actual[0].token = aa.token;
    actual[1].token = bb.token;
    LT_ASSERT(r.commit_datagram(actual, {}));
    LT_CHECK(r.bytes_in_flight() == 1200 && !r.preparation_pending());
LT_END_AUTO_TEST(two_space_preparations_commit_or_abandon_as_one_datagram)
LT_BEGIN_AUTO_TEST(datagram_suite, failed_final_total_keeps_ack_and_flow_unpublished)
    hd::quic_recovery r({}, budget());
    hd::quic_transport_parameters params;
    params.initial_max_data = params.initial_max_stream_data_bidi_remote = 10000;
    params.initial_max_streams_bidi = 1;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, params, params, 4, budget());
    LT_ASSERT(f.open_local(false));
    std::array<std::byte, 1200> one{}, two{};
    LT_ASSERT(r.receive_packet(space::handshake, 0, true, {}));
    LT_ASSERT(r.retain_stream({1, 0, std::span(two).first(10), true}));
    LT_CHECK(r.begin_datagram());
    if (!r.preparation_pending()) return;
    auto a = r.prepare_datagram_packet(space::handshake, one, {}, {800, 40}, f);
    auto b = r.prepare_datagram_packet(space::application, two, {}, {800, 40}, f);
    LT_CHECK(a && b);
    if (!a || !b) return;
    std::array<hd::quic_datagram_emission, 2> actual{{{a.token, 800, false, true}, {b.token, 800, true, true}}};
    LT_CHECK(r.commit_datagram(actual, {}).code == hd::quic_recovery_code::invalid);
    LT_CHECK(r.bytes_in_flight() == 0 && f.sent() == 0 && r.ack_deadline(space::handshake));
    LT_ASSERT(r.abandon_datagram());
LT_END_AUTO_TEST(failed_final_total_keeps_ack_and_flow_unpublished)
LT_BEGIN_AUTO_TEST(datagram_suite, aggregate_pacing_is_rechecked_and_missing_keys_never_stage)
    hd::quic_recovery r({}, budget());
    hd::quic_transport_parameters params;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, params, params, 4, budget());
    std::array<std::byte, 1200> one{}, two{};
    auto p = r.reserve_packet(space::application);
    LT_ASSERT(p && r.commit_sent(p.token, {}, 1500, true, true));
    LT_ASSERT(r.retain_crypto(space::initial, 0, std::span(one).first(10)));
    LT_ASSERT(r.retain_crypto(space::handshake, 0, std::span(two).first(10)));
    LT_CHECK(r.begin_datagram());
    if (!r.preparation_pending()) return;
    auto a = r.prepare_datagram_packet(space::initial, one, {}, {600, 40}, f);
    auto b = r.prepare_datagram_packet(space::handshake, two, {}, {600, 40}, f);
    LT_CHECK(a && b);
    if (!a || !b) return;
    std::array<hd::quic_datagram_emission, 2> actual{{{a.token, 600, true, true}, {b.token, 600, true, true}}};
    LT_CHECK(r.check_datagram_emission(actual, {}).code == hd::quic_recovery_code::pacing_blocked);
    LT_ASSERT(r.abandon_datagram());
    hd::quic_recovery_environment env;
    env.write_keys[0] = false;
    r.set_environment(env, {});
    LT_ASSERT(r.begin_datagram());
    LT_CHECK(r.prepare_datagram_packet(space::initial, one, {}, {600, 40}, f).code == hd::quic_recovery_code::unavailable);
    LT_ASSERT(r.abandon_datagram());
LT_END_AUTO_TEST(aggregate_pacing_is_rechecked_and_missing_keys_never_stage)
LT_BEGIN_AUTO_TEST(datagram_suite, padded_ack_only_packet_must_count_in_flight)
    hd::quic_recovery r({}, budget());
    hd::quic_transport_parameters params;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, params, params, 4, budget());
    std::array<std::byte, 1200> output{};
    LT_ASSERT(r.receive_packet(space::application, 0, true, {}));
    auto p = r.prepare_scheduled_packet(space::application, output, {}, {1200, 40}, f);
    LT_ASSERT(p && !p.ack_eliciting);
    LT_CHECK(r.check_scheduled_emission(p.token, {}, 1200, false, false).code == hd::quic_recovery_code::invalid);
    LT_ASSERT(r.commit_sent(p.token, {}, 1200, false, true));
    LT_CHECK(r.bytes_in_flight() == 1200);
LT_END_AUTO_TEST(padded_ack_only_packet_must_count_in_flight)
LT_BEGIN_AUTO_TEST(datagram_suite, unrelated_preparation_cannot_overwrite_a_staged_stream_buffer)
    hd::quic_recovery r({}, budget());
    hd::quic_transport_parameters params;
    params.initial_max_data = params.initial_max_stream_data_bidi_remote = 1000;
    params.initial_max_streams_bidi = 1;
    hd::quic_flow_control f(hd::quic_endpoint_role::server, params, params, 4, budget());
    std::array<std::byte, 1200> output{};
    LT_ASSERT(f.open_local(false));
    for (unsigned i = 0; i < 2; ++i) {
        LT_ASSERT(r.receive_packet(space::application, i, true, {}));
        auto p = r.prepare_scheduled_packet(space::application, output, {}, {1200, 40}, f);
        LT_ASSERT(p && r.commit_sent(p.token, {}, p.bytes + 40, false, false));
    }
    LT_ASSERT(r.retain_stream({1, 0, std::span(output).first(20), true}));
    LT_ASSERT(r.begin_datagram());
    auto p = r.prepare_datagram_packet(space::application, output, {}, {60, 40}, f);
    LT_ASSERT(p && p.stream && !p.stream->fin);
    const auto prepared = output;
    LT_CHECK(r.prepare_packet(space::application, output, {}, f).code == hd::quic_recovery_code::busy);
    LT_CHECK(output == prepared && f.sent() == 0);
    LT_ASSERT(r.abandon_datagram());
LT_END_AUTO_TEST(unrelated_preparation_cannot_overwrite_a_staged_stream_buffer)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
