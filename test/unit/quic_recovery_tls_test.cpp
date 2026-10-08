/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <array>
#include <vector>
#include <httpserver/detail/quic_recovery.hpp>
#include "./quic_tls_peer.hpp"
#include "./littletest.hpp"
using space = hd::quic_pn_space;
namespace {
hd::quic_ack_frame ack(std::uint64_t number) { return {number, 0, 0, 0, {}, {}}; }
void ping(hd::quic_recovery& r) {
    auto p = r.reserve_packet(space::initial);
    if (!p || !r.commit_sent(p.token, {}, 100, true, true)) throw std::runtime_error("Ping refused");
}
}  // namespace
LT_BEGIN_SUITE(recovery_tls_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(recovery_tls_suite)
LT_BEGIN_AUTO_TEST(recovery_tls_suite, real_tls_crypto_reprotected_after_loss_and_retired_only_through_acked_prefix)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(quic_test::credentials()).ok());
    quic_test::connection connection(registry.acquire()->select_default());
    LT_ASSERT(connection.client.step() && connection.deliver_client());
    auto handshake = connection.server.handshake();
    LT_CHECK(handshake.state != hd::tls_session::progress::failed);
    std::array<std::byte, 4096> crypto{};
    auto copy = connection.server.copy_output(hd::quic_crypto_level::initial, 0, crypto);
    LT_ASSERT(copy && copy.bytes > 20);
    const auto payload = std::span(crypto).first(copy.bytes);
    constexpr std::array dcid{std::byte{8}, std::byte{9}};
    LT_CHECK(connection.keys.install_initial(hd::quic_endpoint_role::server, dcid) == hd::quic_crypto_code::ok);
    hd::quic_key_state receiver;
    LT_CHECK(receiver.install_initial(hd::quic_endpoint_role::client, dcid) == hd::quic_crypto_code::ok);
    hd::quic_recovery recovery({}, httpserver::server::resource_budget::root({}));
    auto information = recovery.retain_crypto(space::initial, 0, payload);
    LT_ASSERT(information);
    std::array<std::byte, 20> small{};
    auto original = recovery.prepare_packet(space::initial, small, {});
    LT_ASSERT(original);
    hd::quic_packet_write packet;
    packet.destination = dcid;
    packet.packet_number = original.packet_number;
    packet.payload = std::span(small).first(original.bytes);
    std::array<std::byte, 4096> wire{}, scratch{}, opened{};
    auto protected_original = connection.keys.protect_packet(hd::quic_key_level::initial, packet, wire, scratch);
    LT_ASSERT(protected_original.code == hd::quic_crypto_code::ok);
    const auto original_wire = std::vector<std::byte>(wire.begin(), wire.begin() + protected_original.consumed);
    LT_ASSERT(recovery.commit_sent(original.token, {}, protected_original.consumed, true, true));
    auto original_tail = recovery.prepare_packet(space::initial, scratch, {});
    LT_ASSERT(original_tail);
    LT_ASSERT(recovery.commit_sent(original_tail.token, {}, 500, true, true));
    for (unsigned n = 0; n < 4; ++n) ping(recovery);
    auto lost = recovery.receive_ack(space::initial, ack(5), {});
    LT_ASSERT(lost);
    LT_CHECK(lost.lost_bytes >= protected_original.consumed + 500);
    auto retransmission = recovery.prepare_packet(space::initial, small, {});
    LT_ASSERT(retransmission);
    LT_CHECK(retransmission.packet_number > original_tail.packet_number);
    packet.packet_number = retransmission.packet_number;
    packet.payload = std::span(small).first(retransmission.bytes);
    auto protected_replacement = connection.keys.protect_packet(hd::quic_key_level::initial, packet, wire, scratch);
    LT_ASSERT(protected_replacement.code == hd::quic_crypto_code::ok);
    LT_CHECK(!std::equal(original_wire.begin(), original_wire.end(), wire.begin(), wire.begin() + protected_replacement.consumed));
    LT_ASSERT(recovery.commit_sent(retransmission.token, {}, protected_replacement.consumed, true, true));
    auto decoded = receiver.open_packet(hd::quic_key_level::initial, std::span(wire).first(protected_replacement.consumed), {}, {}, false, opened, scratch);
    LT_ASSERT(decoded.code == hd::quic_crypto_code::ok);
    LT_CHECK(decoded.header.packet_number == retransmission.packet_number);
    hd::quic_frame_cursor cursor;
    auto frame = hd::next_quic_frame(std::span(opened).first(decoded.payload_size), cursor, {hd::quic_packet_kind::initial, hd::quic_endpoint_role::server});
    LT_ASSERT(frame.code == hd::quic_codec_code::ok);
    auto crypto_frame = std::get<hd::quic_crypto_frame>(frame.value);
    LT_CHECK(crypto_frame.offset == 0 && std::equal(crypto_frame.data.begin(), crypto_frame.data.end(), payload.begin()));
    auto replacement_tail = recovery.prepare_packet(space::initial, scratch, {});
    LT_ASSERT(replacement_tail);
    LT_ASSERT(recovery.commit_sent(replacement_tail.token, {}, 500, true, true));
    LT_ASSERT(recovery.receive_ack(space::initial, ack(replacement_tail.packet_number), {}));
    LT_CHECK(recovery.delivered_prefix(information.id) == 0 && !recovery.take_completion());
    LT_CHECK(connection.server.copy_output(hd::quic_crypto_level::initial, 0, opened).bytes == copy.bytes);
    LT_ASSERT(recovery.receive_ack(space::initial, ack(retransmission.packet_number), {}));
    auto prefix = recovery.delivered_prefix(information.id);
    LT_ASSERT(prefix && *prefix == copy.bytes);
    LT_ASSERT(connection.server.retire_output_prefix(hd::quic_crypto_level::initial, *prefix));
    LT_CHECK(!connection.server.copy_output(hd::quic_crypto_level::initial, 0, opened));
    LT_CHECK(recovery.take_completion().has_value());
    LT_ASSERT(recovery.receive_ack(space::initial, ack(original.packet_number), {}));
    LT_CHECK(!recovery.take_completion());
LT_END_AUTO_TEST(real_tls_crypto_reprotected_after_loss_and_retired_only_through_acked_prefix)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
