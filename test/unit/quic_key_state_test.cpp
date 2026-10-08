/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <type_traits>
#include <httpserver/detail/quic_key_state.hpp>
#include "../../src/detail/quic_crypto_provider.hpp"
#include "./quic_crypto_test_support.hpp"
using level = hd::quic_key_level;
using direction = hd::quic_key_direction;
using code = hd::quic_crypto_code;
constexpr auto cipher_suite = hd::quic_cipher_suite::aes_128_gcm_sha256;
static_assert(!std::is_copy_constructible_v<hd::quic_packet_keys>);
static_assert(!std::is_copy_constructible_v<hd::quic_key_state>);
static auto traffic_secret() { return hex("c00cf151ca5be075ed0ebfb5c80323c42d6b7db67881289af4008f1f6c357aea"); }
static std::vector<std::byte> seal(hd::quic_key_state& state, std::uint64_t number) {
    std::array<std::byte, 128> output{}, scratch{};
    auto payload = hex("01000000");
    hd::quic_packet_write packet;
    packet.kind = hd::quic_packet_kind::one_rtt;
    packet.packet_number = number;
    packet.payload = payload;
    auto result = state.protect_packet(level::application, packet, output, scratch);
    if (result.code != code::ok) return {};
    return {output.begin(), output.begin() + result.consumed};
}
LT_BEGIN_SUITE(state_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(state_suite)
LT_BEGIN_AUTO_TEST(state_suite, directional_installation_rollback_retry_and_discard)
    cleanse_receipt receipt;
    {
        hd::quic_key_state client({}, cleanse_receipt::observe, &receipt), server;
        auto cid = hex("8394c8f03e515708"), secret = traffic_secret();
        LT_ASSERT(client.install_initial(hd::quic_endpoint_role::client, cid) == code::ok);
        LT_ASSERT(server.install_initial(hd::quic_endpoint_role::server, cid) == code::ok);
        LT_CHECK(equals(client.keys(level::initial, direction::write)->key.bytes(), "1f369613dd76d5467730efcbe3b1a22d"));
        LT_CHECK(equals(server.keys(level::initial, direction::read)->key.bytes(), "1f369613dd76d5467730efcbe3b1a22d"));
        LT_CHECK(client.keys(level::handshake, direction::read) == nullptr);
        LT_ASSERT(client.install_traffic_secret(level::handshake, direction::write, cipher_suite, secret) == code::ok);
        LT_CHECK(client.keys(level::handshake, direction::read) == nullptr);
        auto existing = client.keys(level::handshake, direction::write)->key.bytes().data();
        LT_CHECK(client.install_traffic_secret(level::handshake, direction::write, cipher_suite, std::span(secret).first(31)) == code::malformed);
        LT_CHECK(client.keys(level::handshake, direction::write)->key.bytes().data() == existing);
        secret[0] ^= std::byte{1};
        auto before = receipt.releases;
        LT_CHECK(client.install_traffic_secret(level::handshake, direction::write, cipher_suite, secret) == code::ok);
        LT_CHECK(receipt.releases >= before + 4 && receipt.clean);
        before = receipt.releases;
        cid[0] ^= std::byte{1};
        LT_CHECK(client.install_initial(hd::quic_endpoint_role::client, cid) == code::ok);
        LT_CHECK(receipt.releases >= before + 8 && receipt.clean);
        client.discard_level(level::handshake);
        LT_CHECK(client.keys(level::handshake, direction::write) == nullptr);
        LT_CHECK(client.install_traffic_secret(level::handshake, direction::write, cipher_suite, secret) == code::invalid_key_transition);
        client.clear();
        LT_CHECK(client.keys(level::initial, direction::write) == nullptr);
    }
    LT_CHECK(receipt.releases >= 20 && receipt.clean);
LT_END_AUTO_TEST(directional_installation_rollback_retry_and_discard)
LT_BEGIN_AUTO_TEST(state_suite, authenticated_update_reordering_retirement_and_phase_wrap)
    cleanse_receipt receipt;
    hd::quic_key_state sender({}, cleanse_receipt::observe, &receipt), receiver;
    auto secret = traffic_secret();
    LT_ASSERT(sender.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    LT_ASSERT(receiver.install_traffic_secret(level::application, direction::read, cipher_suite, secret) == code::ok);
    auto old = seal(sender, 5), current = seal(sender, 6);
    LT_ASSERT(!old.empty() && !current.empty());
    std::array<std::byte, 128> clear{}, scratch{};
    LT_CHECK(receiver.open_packet(level::application, current, 0, {}, false, clear, scratch).code == code::invalid_key_transition);
    LT_CHECK(receiver.open_packet(level::application, current, 0, {}, true, clear, scratch).code == code::ok);
    auto hp = sender.keys(level::application, direction::write)->hp.bytes();
    std::vector<std::byte> original_hp(hp.begin(), hp.end());
    LT_ASSERT(sender.prepare_next_application_keys(direction::write) == code::ok);
    LT_CHECK(sender.advance_write_keys(false, true) == code::invalid_key_transition);
    LT_CHECK(sender.advance_write_keys(true, false) == code::invalid_key_transition);
    auto before = receipt.releases;
    LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
    LT_CHECK(receipt.releases >= before + 4 && receipt.clean);
    LT_CHECK(std::equal(original_hp.begin(), original_hp.end(), sender.keys(level::application, direction::write)->hp.bytes().begin()));
    auto updated = seal(sender, 7), forged = updated;
    forged.back() ^= std::byte{1};
    clear.fill(std::byte{0x55});
    LT_CHECK(receiver.open_packet(level::application, forged, 0, 6, true, clear, scratch).code == code::authentication_failed);
    LT_CHECK_EQ(receiver.generation(direction::read), std::uint64_t{0});
    LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
    LT_ASSERT(receiver.open_packet(level::application, updated, 0, 6, true, clear, scratch).code == code::ok);
    LT_CHECK_EQ(receiver.generation(direction::read), std::uint64_t{1});
    LT_CHECK(receiver.open_packet(level::application, old, 0, 7, true, clear, scratch).code == code::ok);
    receiver.retire_previous_read_keys();
    LT_CHECK(receiver.open_packet(level::application, old, 0, 7, true, clear, scratch).code != code::ok);
    LT_ASSERT(sender.prepare_next_application_keys(direction::write) == code::ok);
    LT_ASSERT(receiver.prepare_next_application_keys(direction::read) == code::ok);
    LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
    auto wrapped = seal(sender, 8);
    LT_ASSERT(receiver.open_packet(level::application, wrapped, 0, 7, true, clear, scratch).code == code::ok);
    LT_CHECK_EQ(receiver.generation(direction::read), std::uint64_t{2});
    LT_CHECK(receiver.open_packet(level::application, old, 0, 8, true, clear, scratch).code != code::ok);
    // The retired-generation rejection now performs and counts authentication.
    LT_CHECK_EQ(receiver.authentication_failures(), std::uint64_t{3});
LT_END_AUTO_TEST(authenticated_update_reordering_retirement_and_phase_wrap)
LT_BEGIN_AUTO_TEST(state_suite, usage_limits_are_bounded_and_failures_survive_updates)
    hd::quic_key_state sender({2, 2}), receiver({2, 2});
    auto secret = traffic_secret();
    LT_ASSERT(sender.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    LT_ASSERT(receiver.install_traffic_secret(level::application, direction::read, cipher_suite, secret) == code::ok);
    LT_CHECK(!seal(sender, 1).empty());
    auto packet = seal(sender, 2);
    LT_CHECK(!packet.empty());
    LT_CHECK(seal(sender, 3).empty());
    LT_ASSERT(sender.prepare_next_application_keys(direction::write) == code::ok);
    LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
    LT_CHECK(seal(sender, 2).empty());
    auto updated = seal(sender, 3);
    LT_ASSERT(!updated.empty());
    std::array<std::byte, 128> clear{}, scratch{};
    auto bad = packet;
    bad.back() ^= std::byte{1};
    LT_CHECK(receiver.open_packet(level::application, bad, 0, {}, true, clear, scratch).code == code::authentication_failed);
    LT_ASSERT(receiver.open_packet(level::application, updated, 0, {}, true, clear, scratch).code == code::ok);
    LT_CHECK_EQ(receiver.authentication_failures(), std::uint64_t{1});
    bad = updated;
    bad.back() ^= std::byte{1};
    LT_CHECK(receiver.open_packet(level::application, bad, 0, 3, true, clear, scratch).code == code::limit_reached);
    LT_CHECK(receiver.open_packet(level::application, updated, 0, 3, true, clear, scratch).code == code::limit_reached);
    LT_CHECK_EQ(receiver.authentication_failures(), std::uint64_t{2});
LT_END_AUTO_TEST(usage_limits_are_bounded_and_failures_survive_updates)
LT_BEGIN_AUTO_TEST(state_suite, peer_update_requires_matching_write_phase_and_cleanses_retired_read_keys)
    cleanse_receipt receipt;
    hd::quic_key_state sender, receiver({}, cleanse_receipt::observe, &receipt);
    auto secret = traffic_secret();
    LT_ASSERT(sender.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    LT_ASSERT(receiver.install_traffic_secret(level::application, direction::read, cipher_suite, secret) == code::ok);
    LT_ASSERT(receiver.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    LT_CHECK(receiver.install_traffic_secret(level::application, direction::read, cipher_suite, secret) == code::invalid_key_transition);
    auto initial = seal(sender, 1);
    std::array<std::byte, 128> clear{}, scratch{};
    LT_ASSERT(receiver.open_packet(level::application, initial, 0, {}, true, clear, scratch).code == code::ok);
    LT_ASSERT(sender.prepare_next_application_keys(direction::write) == code::ok);
    auto prepared = seal(sender, 2);
    LT_ASSERT(!prepared.empty());
    LT_ASSERT(receiver.open_packet(level::application, prepared, 0, 1, true, clear, scratch).code == code::ok);
    LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
    auto updated = seal(sender, 3);
    LT_ASSERT(receiver.open_packet(level::application, updated, 0, 2, true, clear, scratch).code == code::ok);
    // Transport must respond to the authenticated update before acknowledging it.
    LT_CHECK(seal(receiver, 1).empty());
    LT_CHECK(receiver.respond_to_peer_update(false) == code::invalid_key_transition);
    LT_ASSERT(receiver.respond_to_peer_update(true) == code::ok);
    LT_CHECK_EQ(receiver.generation(direction::write), std::uint64_t{1});
    LT_CHECK(!seal(receiver, 1).empty());
    auto before = receipt.releases;
    receiver.retire_previous_read_keys();
    LT_CHECK(receipt.releases >= before + 4 && receipt.clean);
    receiver.discard_level(level::application);
    LT_CHECK(receiver.prepare_next_application_keys(direction::read) == code::keys_unavailable);
    LT_CHECK(receiver.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::invalid_key_transition);
LT_END_AUTO_TEST(peer_update_requires_matching_write_phase_and_cleanses_retired_read_keys)
LT_BEGIN_AUTO_TEST(state_suite, initial_replacement_preserves_packet_number_ownership_and_exact_limit_codes)
    hd::quic_key_state state({1, 2});
    auto cid = hex("8394c8f03e515708"), payload = hex("01000000");
    LT_ASSERT(state.install_initial(hd::quic_endpoint_role::client, cid) == code::ok);
    hd::quic_packet_write packet;
    packet.destination = cid;
    packet.payload = payload;
    packet.packet_number = 10;
    std::array<std::byte, 128> output{}, scratch{};
    LT_CHECK(state.protect_packet(level::initial, packet, output, scratch).code == code::ok);
    packet.packet_number = 11;
    LT_CHECK(state.protect_packet(level::initial, packet, output, scratch).code == code::limit_reached);
    cid[0] ^= std::byte{1};
    LT_ASSERT(state.install_initial(hd::quic_endpoint_role::client, cid) == code::ok);
    packet.packet_number = 10;
    LT_CHECK(state.protect_packet(level::initial, packet, output, scratch).code == code::invalid_key_transition);
    packet.packet_number = 11;
    LT_CHECK(state.protect_packet(level::initial, packet, output, scratch).code == code::ok);
    auto secret = traffic_secret();
    LT_ASSERT(state.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    packet.kind = hd::quic_packet_kind::one_rtt;
    packet.packet_number = 1;
    LT_CHECK(state.protect_packet(level::application, packet, output, scratch).code == code::ok);
    packet.packet_number = 2;
    LT_CHECK(state.protect_packet(level::application, packet, output, scratch).code == code::update_required);
    LT_CHECK(state.install_traffic_secret(static_cast<level>(99), direction::read, cipher_suite, secret) == code::malformed);
    LT_CHECK(state.prepare_next_application_keys(static_cast<direction>(99)) == code::malformed);
LT_END_AUTO_TEST(initial_replacement_preserves_packet_number_ownership_and_exact_limit_codes)
LT_BEGIN_AUTO_TEST(state_suite, packet_number_transition_errors_require_authentication)
    hd::quic_key_state sender, receiver;
    auto secret = traffic_secret(), payload = hex("01000000");
    LT_ASSERT(sender.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    LT_ASSERT(receiver.install_traffic_secret(level::application, direction::read, cipher_suite, secret) == code::ok);
    std::array<std::byte, 128> clear{}, scratch{}, output{};
    auto old = seal(sender, 10);
    LT_ASSERT(receiver.open_packet(level::application, old, 0, {}, true, clear, scratch).code == code::ok);
    LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
    auto updated = seal(sender, 11);
    LT_ASSERT(receiver.open_packet(level::application, updated, 0, 10, true, clear, scratch).code == code::ok);
    hd::quic_packet_write backwards;
    backwards.kind = hd::quic_packet_kind::one_rtt;
    backwards.packet_number = 9;
    backwards.key_phase = true;
    backwards.payload = payload;
    auto sealed = hd::protect_quic_packet(*sender.keys(level::application, direction::write), backwards, output, scratch);
    LT_ASSERT(sealed.code == code::ok);
    auto wire = std::span(output).first(sealed.consumed);
    clear.fill(std::byte{0x55});
    LT_CHECK(receiver.open_packet(level::application, wire, 0, 11, true, clear, scratch).code == code::invalid_key_transition);
    wire.back() ^= std::byte{1};
    LT_CHECK(receiver.open_packet(level::application, wire, 0, 11, true, clear, scratch).code == code::authentication_failed);
    LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
    LT_CHECK_EQ(receiver.generation(direction::read), std::uint64_t{1});
LT_END_AUTO_TEST(packet_number_transition_errors_require_authentication)
LT_BEGIN_AUTO_TEST(state_suite, reordered_previous_packets_advance_authenticated_generation_boundary)
    hd::quic_key_state sender, receiver;
    auto secret = traffic_secret(), payload = hex("01000000");
    LT_ASSERT(sender.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    LT_ASSERT(receiver.install_traffic_secret(level::application, direction::read, cipher_suite, secret) == code::ok);
    auto old10 = seal(sender, 10), old19 = seal(sender, 19);
    std::array<std::byte, 128> clear{}, scratch{}, output{};
    LT_ASSERT(receiver.open_packet(level::application, old10, 0, {}, true, clear, scratch).code == code::ok);
    LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
    auto new20 = seal(sender, 20);
    LT_ASSERT(receiver.open_packet(level::application, new20, 0, 10, true, clear, scratch).code == code::ok);
    LT_ASSERT(receiver.open_packet(level::application, old19, 0, 20, true, clear, scratch).code == code::ok);
    hd::quic_packet_write packet;
    packet.kind = hd::quic_packet_kind::one_rtt;
    packet.packet_number = 15;
    packet.key_phase = true;
    packet.payload = payload;
    auto sealed = hd::protect_quic_packet(*sender.keys(level::application, direction::write), packet, output, scratch);
    LT_ASSERT(sealed.code == code::ok);
    auto wire = std::span(output).first(sealed.consumed);
    clear.fill(std::byte{0x55});
    LT_CHECK(receiver.open_packet(level::application, wire, 0, 20, true, clear, scratch).code == code::invalid_key_transition);
    LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
    wire.back() ^= std::byte{1};
    LT_CHECK(receiver.open_packet(level::application, wire, 0, 20, true, clear, scratch).code == code::authentication_failed);
    LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
    LT_CHECK_EQ(receiver.generation(direction::read), std::uint64_t{1});
    auto new21 = seal(sender, 21);
    LT_CHECK(receiver.open_packet(level::application, new21, 0, 20, true, clear, scratch).code == code::ok);
    LT_CHECK(equals(std::span(clear).first(payload.size()), "01000000"));
LT_END_AUTO_TEST(reordered_previous_packets_advance_authenticated_generation_boundary)
LT_BEGIN_AUTO_TEST(state_suite, absent_generations_authenticate_and_reject_without_key_derivation)
    for (auto suite : {hd::quic_cipher_suite::aes_128_gcm_sha256, hd::quic_cipher_suite::aes_256_gcm_sha384, hd::quic_cipher_suite::chacha20_poly1305_sha256}) {
        cleanse_receipt receipt;
        hd::quic_key_state sender, receiver({}, cleanse_receipt::observe, &receipt);
        std::array<std::byte, 48> material{};
        auto secret = std::span(material).first(suite == hd::quic_cipher_suite::aes_256_gcm_sha384 ? 48 : 32);
        LT_ASSERT(sender.install_traffic_secret(level::application, direction::write, suite, secret) == code::ok);
        LT_ASSERT(receiver.install_traffic_secret(level::application, direction::read, suite, secret) == code::ok);
        auto old = seal(sender, 10);
        std::array<std::byte, 128> clear{}, scratch{};
        LT_ASSERT(receiver.open_packet(level::application, old, 0, {}, true, clear, scratch).code == code::ok);
        LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
        auto current = seal(sender, 20);
        LT_ASSERT(receiver.open_packet(level::application, current, 0, 10, true, clear, scratch).code == code::ok);
        receiver.retire_previous_read_keys();
        auto releases = receipt.releases;
        clear.fill(std::byte{0x55});
        scratch.fill(std::byte{0x66});
        LT_CHECK(receiver.open_packet(level::application, old, 0, 20, true, clear, scratch).code == code::authentication_failed);
        LT_CHECK(zeros(std::span(scratch).first(old.size())));
        LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
        LT_CHECK_EQ(receiver.authentication_failures(), std::uint64_t{1});
        LT_ASSERT(sender.advance_write_keys(true, true) == code::ok);
        auto next = seal(sender, 30);
        scratch.fill(std::byte{0x66});
        LT_CHECK(receiver.open_packet(level::application, next, 0, 20, true, clear, scratch).code == code::authentication_failed);
        LT_CHECK(zeros(std::span(scratch).first(next.size())));
        LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
        LT_CHECK_EQ(receiver.authentication_failures(), std::uint64_t{2});
        LT_CHECK_EQ(receiver.generation(direction::read), std::uint64_t{1});
        LT_CHECK_EQ(receipt.releases, releases);
        LT_ASSERT(receiver.open_packet(level::application, current, 0, 20, true, clear, scratch).code == code::ok);
        LT_ASSERT(receiver.prepare_next_application_keys(direction::read) == code::ok);
        LT_CHECK(receiver.open_packet(level::application, next, 0, 20, true, clear, scratch).code == code::ok);
        LT_CHECK_EQ(receiver.generation(direction::read), std::uint64_t{2});
        receiver.clear();
        LT_CHECK(receipt.clean);
    }
LT_END_AUTO_TEST(absent_generations_authenticate_and_reject_without_key_derivation)
LT_BEGIN_AUTO_TEST(state_suite, unavailable_generation_never_publishes_even_authenticated_plaintext)
    for (auto suite : {hd::quic_cipher_suite::aes_128_gcm_sha256, hd::quic_cipher_suite::aes_256_gcm_sha384, hd::quic_cipher_suite::chacha20_poly1305_sha256}) {
        std::array<std::byte, 48> secret{};
        auto bytes = std::span(secret).first(suite == hd::quic_cipher_suite::aes_256_gcm_sha384 ? 48 : 32);
        hd::quic_packet_keys keys;
        LT_ASSERT(hd::derive_quic_packet_keys(suite, bytes, keys) == code::ok);
        auto payload = hex("01000000");
        hd::quic_packet_write packet;
        packet.kind = hd::quic_packet_kind::one_rtt;
        packet.packet_number = 30;
        packet.key_phase = true;
        packet.payload = payload;
        std::array<std::byte, 128> output{}, clear{}, scratch{};
        auto sealed = hd::protect_quic_packet(keys, packet, output, scratch);
        LT_ASSERT(sealed.code == code::ok);
        auto wire = std::span(output).first(sealed.consumed);
        LT_ASSERT(hd::unprotect_quic_packet(keys, wire, 0, 20, clear, scratch).code == code::ok);
        clear.fill(std::byte{0x55});
        auto rejected = hd::quic_open_checked(keys, wire, 0, 20, clear, scratch, {hd::quic_reject_unavailable, nullptr});
        LT_CHECK(rejected.code == code::authentication_failed);
        LT_CHECK_EQ(rejected.consumed, std::size_t{0});
        LT_CHECK_EQ(rejected.payload_size, std::size_t{0});
        LT_CHECK(zeros(std::span(scratch).first(wire.size())));
        LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
        wire.back() ^= std::byte{1};
        LT_CHECK(hd::quic_open_checked(keys, wire, 0, 20, clear, scratch, {hd::quic_reject_unavailable, nullptr}).code == code::authentication_failed);
        LT_ASSERT(EVP_set_default_properties(nullptr, "provider=task152_missing_provider") == 1);
        auto failed = hd::quic_open_checked(keys, wire, 0, 20, clear, scratch, {hd::quic_reject_unavailable, nullptr});
        auto restored = EVP_set_default_properties(nullptr, "");
        LT_CHECK_EQ(restored, 1);
        LT_CHECK(failed.code == code::provider_failure);
        LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
    }
LT_END_AUTO_TEST(unavailable_generation_never_publishes_even_authenticated_plaintext)
LT_BEGIN_AUTO_TEST(state_suite, application_read_installation_failure_is_transactional)
    hd::quic_key_state state;
    auto secret = traffic_secret();
    LT_ASSERT(state.install_traffic_secret(level::application, direction::write, cipher_suite, secret) == code::ok);
    auto original = state.keys(level::application, direction::write)->key.bytes();
    std::vector<std::byte> original_key(original.begin(), original.end());
    LT_ASSERT(EVP_set_default_properties(nullptr, "provider=task152_missing_provider") == 1);
    auto failed = state.install_traffic_secret(level::application, direction::read, cipher_suite, secret);
    auto restored = EVP_set_default_properties(nullptr, "");
    LT_CHECK_EQ(restored, 1);
    LT_CHECK(failed == code::provider_failure);
    LT_CHECK(state.keys(level::application, direction::read) == nullptr);
    LT_CHECK(std::equal(original_key.begin(), original_key.end(), state.keys(level::application, direction::write)->key.bytes().begin()));
    LT_CHECK(!seal(state, 1).empty());
    LT_ASSERT(state.install_traffic_secret(level::application, direction::read, cipher_suite, secret) == code::ok);
    auto wire = seal(state, 2);
    std::array<std::byte, 128> clear{}, scratch{};
    LT_CHECK(state.open_packet(level::application, wire, 0, {}, true, clear, scratch).code == code::ok);
LT_END_AUTO_TEST(application_read_installation_failure_is_transactional)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
