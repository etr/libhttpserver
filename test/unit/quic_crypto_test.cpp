/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/evp.h>
#include <vector>
#include "./quic_crypto_test_support.hpp"
LT_BEGIN_SUITE(crypto_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(crypto_suite)
LT_BEGIN_AUTO_TEST(crypto_suite, rfc_initial_derivation_and_transactional_failure)
    cleanse_receipt receipt;
    {
        hd::quic_initial_keys keys;
        LT_ASSERT(hd::derive_quic_initial_keys(hex("8394c8f03e515708"), keys, cleanse_receipt::observe, &receipt) == hd::quic_crypto_code::ok);
        LT_CHECK(equals(keys.client.secret.bytes(), "c00cf151ca5be075ed0ebfb5c80323c42d6b7db67881289af4008f1f6c357aea"));
        LT_CHECK(equals(keys.client.key.bytes(), "1f369613dd76d5467730efcbe3b1a22d"));
        LT_CHECK(equals(keys.client.iv.bytes(), "fa044b2f42a3fd3b46fb255c"));
        LT_CHECK(equals(keys.client.hp.bytes(), "9f50449e04a0e810283a1e9933adedd2"));
        LT_CHECK(equals(keys.server.secret.bytes(), "3c199828fd139efd216c155ad844cc81fb82fa8d7446fa7d78be803acdda951b"));
        LT_CHECK(equals(keys.server.key.bytes(), "cf3a5331653c364c88f0f379b6067e37"));
        LT_CHECK(equals(keys.server.iv.bytes(), "0ac1493ca1905853b0bba03e"));
        LT_CHECK(equals(keys.server.hp.bytes(), "c206b8d9b9f0f37644430b490eeaa314"));
        std::array<std::byte, 21> invalid{};
        LT_CHECK(hd::derive_quic_initial_keys(invalid, keys) == hd::quic_crypto_code::malformed);
        LT_CHECK(equals(keys.client.key.bytes(), "1f369613dd76d5467730efcbe3b1a22d"));
    }
    LT_CHECK(receipt.releases >= 8 && receipt.clean);
LT_END_AUTO_TEST(rfc_initial_derivation_and_transactional_failure)
LT_BEGIN_AUTO_TEST(crypto_suite, rfc_chacha_derivation_and_update)
    hd::quic_packet_keys keys, next;
    auto secret = hex("9ac312a7f877468ebe69422748ad00a15443f18203a07d6060f688f30f21632b");
    LT_ASSERT(hd::derive_quic_packet_keys(hd::quic_cipher_suite::chacha20_poly1305_sha256, secret, keys) == hd::quic_crypto_code::ok);
    LT_CHECK(equals(keys.key.bytes(), "c6d98ff3441c3fe1b2182094f69caa2ed4b716b65488960a7a984979fb23e1c8"));
    LT_CHECK(equals(keys.iv.bytes(), "e0459b3474bdd0e44a41c144"));
    LT_CHECK(equals(keys.hp.bytes(), "25a282b9e82f06f21f488917a4fc8f1b73573685608597d0efcb076b0ab7a7a4"));
    LT_ASSERT(hd::derive_quic_next_keys(keys, next) == hd::quic_crypto_code::ok);
    LT_CHECK(equals(next.secret.bytes(), "1223504755036d556342ee9361d253421a826c9ecdf3c7148684b36b714881f9"));
    LT_CHECK(std::equal(keys.hp.bytes().begin(), keys.hp.bytes().end(), next.hp.bytes().begin()));
    LT_CHECK(hd::derive_quic_packet_keys(keys.suite, std::span(secret).first(31), next) == hd::quic_crypto_code::malformed);
    LT_CHECK(hd::derive_quic_packet_keys(static_cast<hd::quic_cipher_suite>(99), secret, next) == hd::quic_crypto_code::unsupported_suite);
LT_END_AUTO_TEST(rfc_chacha_derivation_and_update)
LT_BEGIN_AUTO_TEST(crypto_suite, rfc_initial_packets_and_masks)
    hd::quic_initial_keys initial;
    auto cid = hex("8394c8f03e515708"), source = hex("f067a5502a4262b5");
    LT_ASSERT(hd::derive_quic_initial_keys(cid, initial) == hd::quic_crypto_code::ok);
    for (bool server : {false, true}) {
        const auto& keys = server ? initial.server : initial.client;
        auto payload = hex(server ? server_payload : client_payload);
        if (!server) payload.resize(1162);
        auto expected = hex(server ? server_packet : client_packet);
        hd::quic_packet_write packet;
        packet.destination = server ? std::span<const std::byte>{} : cid;
        packet.source = server ? std::span<const std::byte>(source) : std::span<const std::byte>{};
        packet.packet_number = server ? 1 : 2;
        packet.packet_number_width = server ? 2 : 4;
        packet.length_width = 2;
        packet.payload = payload;
        std::array<std::byte, 1600> output{}, scratch{}, clear{};
        auto sealed = hd::protect_quic_packet(keys, packet, output, scratch);
        LT_ASSERT(sealed.code == hd::quic_crypto_code::ok);
        LT_CHECK(std::equal(expected.begin(), expected.end(), output.begin(), output.begin() + sealed.consumed));
        LT_CHECK(zeros(scratch));
        auto datagram = expected;
        datagram.insert(datagram.end(), expected.begin(), expected.end());
        auto opened = hd::unprotect_quic_packet(keys, datagram, {}, {}, clear, scratch);
        LT_ASSERT(opened.code == hd::quic_crypto_code::ok);
        LT_CHECK_EQ(opened.consumed, expected.size());
        LT_CHECK_EQ(opened.header.packet_number, packet.packet_number);
        LT_CHECK_EQ(opened.payload_size, payload.size());
        LT_CHECK(std::equal(payload.begin(), payload.end(), clear.begin()));
        std::array<std::byte, 5> mask{};
        LT_CHECK(hd::quic_header_mask(keys, hex(server ? "2cd0991cd25b0aac406a5816b6394100" : "d1b1c98dd7689fb8ec11d242b123dc9b"), mask) == hd::quic_crypto_code::ok);
        LT_CHECK(equals(mask, server ? "2ec0d8356a" : "437b9aec36"));
    }
LT_END_AUTO_TEST(rfc_initial_packets_and_masks)
LT_BEGIN_AUTO_TEST(crypto_suite, rfc_chacha_packet_nonce_and_mask)
    hd::quic_packet_keys keys;
    LT_ASSERT(hd::derive_quic_packet_keys(hd::quic_cipher_suite::chacha20_poly1305_sha256,
              hex("9ac312a7f877468ebe69422748ad00a15443f18203a07d6060f688f30f21632b"), keys) == hd::quic_crypto_code::ok);
    std::array<std::byte, 12> nonce{};
    std::array<std::byte, 5> mask{};
    LT_CHECK(hd::quic_packet_nonce(keys, 654360564, nonce) == hd::quic_crypto_code::ok);
    LT_CHECK(equals(nonce, "e0459b3474bdd0e46d417eb0"));
    LT_CHECK(hd::quic_header_mask(keys, hex("5e5cd55c41f69080575d7999c25a5bfb"), mask) == hd::quic_crypto_code::ok);
    LT_CHECK(equals(mask, "aefefe7d03"));
    auto payload = hex("01"), expected = hex("4cfe4189655e5cd55c41f69080575d7999c25a5bfb");
    hd::quic_packet_write packet;
    packet.kind = hd::quic_packet_kind::one_rtt;
    packet.packet_number = 654360564;
    packet.packet_number_width = 3;
    packet.payload = payload;
    std::array<std::byte, 128> output{}, scratch{}, clear{};
    auto sealed = hd::protect_quic_packet(keys, packet, output, scratch);
    LT_ASSERT(sealed.code == hd::quic_crypto_code::ok);
    LT_CHECK(std::equal(expected.begin(), expected.end(), output.begin(), output.begin() + sealed.consumed));
    auto opened = hd::unprotect_quic_packet(keys, expected, 0, 654360563, clear, scratch);
    LT_ASSERT(opened.code == hd::quic_crypto_code::ok);
    LT_CHECK_EQ(opened.header.packet_number, std::uint64_t{654360564});
    LT_CHECK(clear[0] == std::byte{1});
LT_END_AUTO_TEST(rfc_chacha_packet_nonce_and_mask)
LT_BEGIN_AUTO_TEST(crypto_suite, rfc_retry_integrity)
    auto retry = hex(retry_packet), cid = hex("8394c8f03e515708");
    std::array<std::byte, 16> tag{};
    LT_CHECK(hd::compute_quic_retry_tag(cid, std::span(retry).first(retry.size() - 16), tag) == hd::quic_crypto_code::ok);
    LT_CHECK(equals(tag, "04a265ba2eff4d829058fb3f0f2496ba"));
    LT_CHECK(hd::verify_quic_retry_tag(cid, retry) == hd::quic_crypto_code::ok);
    for (auto offset : {std::size_t{0}, std::size_t{15}, retry.size() - 1}) {
        retry[offset] ^= std::byte{1};
        LT_CHECK(hd::verify_quic_retry_tag(cid, retry) != hd::quic_crypto_code::ok);
        retry[offset] ^= std::byte{1};
    }
    cid[0] ^= std::byte{1};
    LT_CHECK(hd::verify_quic_retry_tag(cid, retry) == hd::quic_crypto_code::authentication_failed);
    LT_CHECK(hd::verify_quic_retry_tag(cid, std::span(retry).first(10)) != hd::quic_crypto_code::ok);
    tag.fill(std::byte{0x55});
    LT_CHECK(hd::compute_quic_retry_tag(cid, std::span(retry).first(retry.size() - 16), std::span(tag).first(15)) == hd::quic_crypto_code::no_space);
    LT_CHECK(std::all_of(tag.begin(), tag.end(), [](auto b) { return b == std::byte{0x55}; }));
LT_END_AUTO_TEST(rfc_retry_integrity)
LT_BEGIN_AUTO_TEST(crypto_suite, independent_aes256_sha384_and_authenticated_reserved_bits)
    std::array<std::byte, 48> secret{};
    for (std::size_t i = 0; i < secret.size(); ++i) secret[i] = std::byte(i);
    hd::quic_packet_keys keys;
    LT_ASSERT(hd::derive_quic_packet_keys(hd::quic_cipher_suite::aes_256_gcm_sha384, secret, keys) == hd::quic_crypto_code::ok);
    // Pinned independently with Python hmac/SHA384 and cryptography AESGCM/ECB.
    LT_CHECK(equals(keys.key.bytes(), "95c517eea81b6469ff8f27a065fd04c1a27b3023591b93e273a9df5f921d1f68"));
    LT_CHECK(equals(keys.iv.bytes(), "a8d8316bf5bb0bbfa74cbf17"));
    LT_CHECK(equals(keys.hp.bytes(), "307135de335efef95873468a03d3dfa1e38050df7cc6ab7f22fd7aced73b66e5"));
    auto payload = hex("01020304"), cid = hex("abcd");
    auto expected = hex("55abcd64625b2aca2a5cee0a24d225950ea476e01e5e1be9e5cc26");
    hd::quic_packet_write packet;
    packet.kind = hd::quic_packet_kind::one_rtt;
    packet.destination = cid;
    packet.packet_number = 0x0102030405060708ULL;
    packet.packet_number_width = 4;
    packet.payload = payload;
    std::array<std::byte, 128> output{}, scratch{}, clear{};
    auto sealed = hd::protect_quic_packet(keys, packet, output, scratch);
    LT_ASSERT(sealed.code == hd::quic_crypto_code::ok);
    LT_CHECK(std::equal(expected.begin(), expected.end(), output.begin(), output.begin() + sealed.consumed));
    LT_CHECK(hd::unprotect_quic_packet(keys, expected, 2, packet.packet_number - 1, clear, scratch).code == hd::quic_crypto_code::ok);
    auto reserved = hex("40abcd07df1228ca2a5cee2e260cc1b92f82b690d452a3d5567e81");
    clear.fill(std::byte{0x55});
    LT_CHECK(hd::unprotect_quic_packet(keys, reserved, 2, packet.packet_number - 1, clear, scratch).code == hd::quic_crypto_code::malformed);
    reserved.back() ^= std::byte{1};
    LT_CHECK(hd::unprotect_quic_packet(keys, reserved, 2, packet.packet_number - 1, clear, scratch).code == hd::quic_crypto_code::authentication_failed);
    LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
    LT_CHECK(zeros(scratch));
LT_END_AUTO_TEST(independent_aes256_sha384_and_authenticated_reserved_bits)
LT_BEGIN_AUTO_TEST(crypto_suite, packet_widths_bounds_overlap_and_tampering)
    hd::quic_initial_keys initial;
    auto cid = hex("8394c8f03e515708"), payload = hex("01000000");
    LT_ASSERT(hd::derive_quic_initial_keys(cid, initial) == hd::quic_crypto_code::ok);
    for (std::size_t width = 1; width <= 4; ++width) {
        hd::quic_packet_write packet;
        packet.destination = cid;
        packet.payload = payload;
        packet.packet_number_width = width;
        packet.packet_number = hd::k_quic_max_integer;
        std::array<std::byte, 128> output{}, scratch{}, clear{};
        auto sealed = hd::protect_quic_packet(initial.client, packet, output, scratch);
        LT_ASSERT(sealed.code == hd::quic_crypto_code::ok);
        auto wire = std::span(output).first(sealed.consumed);
        LT_CHECK(hd::unprotect_quic_packet(initial.client, wire, {}, packet.packet_number - 1, clear, scratch).code == hd::quic_crypto_code::ok);
        LT_CHECK(hd::unprotect_quic_packet(initial.server, wire, {}, packet.packet_number - 1, clear, scratch).code == hd::quic_crypto_code::authentication_failed);
        for (auto offset : {std::size_t{6}, sealed.consumed - 17, sealed.consumed - 1}) {
            output[offset] ^= std::byte{1};
            clear.fill(std::byte{0x55});
            auto result = hd::unprotect_quic_packet(initial.client, wire, {}, packet.packet_number - 1, clear, scratch);
            LT_CHECK(result.code != hd::quic_crypto_code::ok);
            LT_CHECK_EQ(result.consumed, std::size_t{0});
            LT_CHECK(std::all_of(clear.begin(), clear.end(), [](auto b) { return b == std::byte{0x55}; }));
            output[offset] ^= std::byte{1};
        }
        LT_CHECK(hd::unprotect_quic_packet(initial.client, wire, {}, packet.packet_number - 1, wire, scratch).code == hd::quic_crypto_code::malformed);
        LT_CHECK(hd::unprotect_quic_packet(initial.client, wire, {}, packet.packet_number - 1, clear, std::span(scratch).first(1)).code == hd::quic_crypto_code::no_space);
        LT_CHECK(hd::unprotect_quic_packet(initial.client, wire, {}, packet.packet_number - 1, std::span(clear).first(1), scratch).code == hd::quic_crypto_code::no_space);
        output.fill(std::byte{0x55});
        LT_CHECK(hd::protect_quic_packet(initial.client, packet, std::span(output).first(1), scratch).code == hd::quic_crypto_code::no_space);
        LT_CHECK(hd::protect_quic_packet(initial.client, packet, output, std::span(scratch).first(1)).code == hd::quic_crypto_code::no_space);
        LT_CHECK(hd::protect_quic_packet(initial.client, packet, output, output).code == hd::quic_crypto_code::malformed);
        packet.payload = std::span(output).first(4);
        LT_CHECK(hd::protect_quic_packet(initial.client, packet, output, scratch).code == hd::quic_crypto_code::malformed);
        packet.payload = payload;
        std::array<std::byte, 16> tag{};
        packet.tag = tag;
        LT_CHECK(hd::protect_quic_packet(initial.client, packet, output, scratch).code == hd::quic_crypto_code::malformed);
        LT_CHECK(std::all_of(output.begin(), output.end(), [](auto b) { return b == std::byte{0x55}; }));
    }
    std::array<std::byte, 12> nonce{};
    LT_CHECK(hd::quic_packet_nonce(initial.client, hd::k_quic_max_integer, nonce) == hd::quic_crypto_code::ok);
    LT_CHECK(equals(nonce, "fa044b2f7d5c02c4b904daa3"));
    LT_CHECK(hd::quic_packet_nonce(initial.client, hd::k_quic_max_integer + 1, nonce) == hd::quic_crypto_code::malformed);
    LT_CHECK(hd::quic_packet_nonce(initial.client, 0, std::span(nonce).first(11)) == hd::quic_crypto_code::no_space);
    std::vector<std::byte> oversized(65536);
    std::array<std::byte, 128> output{}, scratch{};
    LT_CHECK(hd::unprotect_quic_packet(initial.client, oversized, {}, {}, output, scratch).code == hd::quic_crypto_code::limit_reached);
    hd::quic_packet_write packet;
    packet.payload = oversized;
    LT_CHECK(hd::protect_quic_packet(initial.client, packet, output, scratch).code == hd::quic_crypto_code::limit_reached);
    packet.payload = {};
    LT_CHECK(hd::protect_quic_packet(initial.client, packet, output, scratch).code == hd::quic_crypto_code::truncated);
    packet.kind = hd::quic_packet_kind::zero_rtt;
    LT_CHECK(hd::protect_quic_packet(initial.client, packet, output, scratch).code == hd::quic_crypto_code::invalid_key_transition);
    auto short_sample = hex("c000000001000000110100000000000000000000000000000000");
    LT_CHECK(hd::unprotect_quic_packet(initial.client, short_sample, {}, {}, output, scratch).code == hd::quic_crypto_code::truncated);
LT_END_AUTO_TEST(packet_widths_bounds_overlap_and_tampering)
LT_BEGIN_AUTO_TEST(crypto_suite, provider_failure_preserves_existing_keys_and_output)
    cleanse_receipt receipt;
    hd::quic_initial_keys initial;
    auto cid = hex("8394c8f03e515708"), payload = hex("01000000"), retry = hex(retry_packet);
    LT_ASSERT(hd::derive_quic_initial_keys(cid, initial, cleanse_receipt::observe, &receipt) == hd::quic_crypto_code::ok);
    auto original_key = initial.client.key.bytes().data();
    // This test process is serialized; restrict fetches without replacing EVP.
    LT_ASSERT(EVP_set_default_properties(nullptr, "provider=task152_missing_provider") == 1);
    auto derivation = hd::derive_quic_initial_keys(cid, initial, cleanse_receipt::observe, &receipt);
    std::array<std::byte, 128> output, scratch;
    output.fill(std::byte{0x55});
    scratch.fill(std::byte{0x33});
    hd::quic_packet_write packet;
    packet.destination = cid;
    packet.payload = payload;
    auto sealed = hd::protect_quic_packet(initial.client, packet, output, scratch);
    auto tag = hd::compute_quic_retry_tag(cid, std::span(retry).first(retry.size() - 16), std::span(output).first(16));
    const auto restored = EVP_set_default_properties(nullptr, "");
    LT_CHECK_EQ(restored, 1);
    LT_CHECK(derivation == hd::quic_crypto_code::provider_failure);
    LT_CHECK(initial.client.key.bytes().data() == original_key);
    LT_CHECK(sealed.code == hd::quic_crypto_code::provider_failure);
    LT_CHECK(tag == hd::quic_crypto_code::provider_failure);
    LT_CHECK(std::all_of(output.begin(), output.end(), [](auto b) { return b == std::byte{0x55}; }));
    LT_CHECK(zeros(scratch));
    LT_CHECK(receipt.clean);
    auto short_sample = hex("c000000001000000110100000000000000000000000000000000");
    auto coalesced = short_sample;
    coalesced.insert(coalesced.end(), retry.begin(), retry.end());
    LT_CHECK(hd::unprotect_quic_packet(initial.client, coalesced, {}, {}, output, scratch).code == hd::quic_crypto_code::truncated);
LT_END_AUTO_TEST(provider_failure_preserves_existing_keys_and_output)
LT_BEGIN_AUTO_TEST(crypto_suite, initial_packets_require_the_fixed_v1_cipher_suite)
    std::array<std::byte, 48> secret{};
    hd::quic_packet_keys keys;
    LT_ASSERT(hd::derive_quic_packet_keys(hd::quic_cipher_suite::aes_256_gcm_sha384, secret, keys) == hd::quic_crypto_code::ok);
    keys.level = hd::quic_key_level::initial;
    auto payload = hex("01000000");
    hd::quic_packet_write packet;
    packet.payload = payload;
    std::array<std::byte, 128> output{}, scratch{};
    LT_CHECK(hd::protect_quic_packet(keys, packet, output, scratch).code == hd::quic_crypto_code::invalid_key_transition);
LT_END_AUTO_TEST(initial_packets_require_the_fixed_v1_cipher_suite)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
