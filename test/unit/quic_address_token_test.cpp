/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/evp.h>
#include <httpserver/detail/quic_address_token.hpp>
#include "./quic_crypto_test_support.hpp"
static hd::quic_cid token_cid(unsigned value) {
    hd::quic_cid result;
    result.size = 8;
    result.bytes[0] = std::byte(value);
    return result;
}
static hd::quic_path_identity token_path() {
    hd::io_datagram packet;
    packet.peer.peer.address = *httpserver::net::parse_address("fe80::1");
    packet.peer.peer.port = 9000;
    packet.peer.scope = 3;
    packet.socket_id = 5;
    return *hd::quic_datagram_path(packet);
}
LT_BEGIN_SUITE(token_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(token_suite)
LT_BEGIN_AUTO_TEST(token_suite, roundtrip_is_bound_to_original_retry_and_full_path)
    std::array<std::byte, 32> key{};
    hd::quic_address_token tokens(key);
    hd::quic_token_claims claims{token_cid(1), token_cid(2)};
    auto path = token_path();
    auto token = tokens.issue(claims, path, 7, 100);
    LT_ASSERT(token);
    LT_CHECK(token->size() <= hd::quic_address_token::maximum_size);
    auto verified = tokens.verify(*token, path, 7, claims.retry_source, 110);
    LT_ASSERT(verified);
    LT_CHECK(verified->original_destination == claims.original_destination);
    LT_CHECK(verified->retry_source == claims.retry_source);
    LT_CHECK(!tokens.verify(*token, path, 7, token_cid(3), 100));
    LT_CHECK(!tokens.verify(*token, path, 8, claims.retry_source, 100));
    LT_CHECK(!tokens.verify(*token, path, 7, claims.retry_source, 99));
    LT_CHECK(!tokens.verify(*token, path, 7, claims.retry_source, 111));
    for (int field = 0; field < 6; ++field) {
        auto changed = path;
        if (field == 0) changed.peer.peer.port++;
        if (field == 1) changed.peer.peer.address.bytes[15] = std::byte{9};
        if (field == 2) changed.peer.scope++;
        if (field == 3) changed.socket_id++;
        if (field == 4) changed.interface_index = 1;
        if (field == 5) changed.local = path.peer;
        LT_CHECK(!tokens.verify(*token, changed, 7, claims.retry_source, 100));
    }
LT_END_AUTO_TEST(roundtrip_is_bound_to_original_retry_and_full_path)
LT_BEGIN_AUTO_TEST(token_suite, present_local_endpoint_and_interface_values_are_authenticated)
    std::array<std::byte, 32> key{};
    hd::quic_address_token tokens(key);
    hd::quic_token_claims claims{token_cid(1), token_cid(2)};
    auto path = token_path();
    path.local = path.peer;
    path.local->peer.address = *httpserver::net::parse_address("fe80::2");
    path.local->peer.port = 443;
    path.interface_index = 3;
    auto token = tokens.issue(claims, path, 7, 100);
    LT_ASSERT(token);
    auto verified = tokens.verify(*token, path, 7, claims.retry_source, 100);
    LT_ASSERT(verified);
    LT_CHECK(verified->original_destination == claims.original_destination);
    LT_CHECK(verified->retry_source == claims.retry_source);

    auto changed_address = path;
    changed_address.local->peer.address = *httpserver::net::parse_address("fe80::3");
    LT_CHECK(!tokens.verify(*token, changed_address, 7, claims.retry_source, 100));
    auto changed_port = path;
    changed_port.local->peer.port++;
    LT_CHECK(!tokens.verify(*token, changed_port, 7, claims.retry_source, 100));
    auto changed_scope = path;
    changed_scope.local->scope++;
    LT_CHECK(!tokens.verify(*token, changed_scope, 7, claims.retry_source, 100));
    auto changed_interface = path;
    (*changed_interface.interface_index)++;
    LT_CHECK(!tokens.verify(*token, changed_interface, 7, claims.retry_source, 100));
LT_END_AUTO_TEST(present_local_endpoint_and_interface_values_are_authenticated)
LT_BEGIN_AUTO_TEST(token_suite, every_truncation_and_mutation_fails_closed)
    hd::quic_address_token tokens;
    hd::quic_token_claims claims{token_cid(1), token_cid(2)};
    auto path = token_path();
    auto token = tokens.issue(claims, path, 7, 100);
    LT_ASSERT(token);
    for (std::size_t i = 0; i < token->size(); ++i) {
        LT_CHECK(!tokens.verify(std::span(*token).first(i), path, 7, claims.retry_source, 100));
        auto changed = *token;
        changed[i] ^= std::byte{1};
        LT_CHECK(!tokens.verify(changed, path, 7, claims.retry_source, 100));
    }
    auto trailing = *token;
    trailing.push_back(std::byte{0});
    LT_CHECK(!tokens.verify(trailing, path, 7, claims.retry_source, 100));
    hd::quic_address_token other;
    LT_CHECK(!other.verify(*token, path, 7, claims.retry_source, 100));
    claims.original_destination.size = 21;
    LT_CHECK(!tokens.issue(claims, path, 7, 100));
LT_END_AUTO_TEST(every_truncation_and_mutation_fails_closed)
LT_BEGIN_AUTO_TEST(token_suite, provider_failure_does_not_issue_or_verify_and_key_sizes_are_strict)
    std::array<std::byte, 32> key{};
    hd::quic_address_token tokens(key);
    hd::quic_token_claims claims{token_cid(1), token_cid(2)};
    auto path = token_path();
    auto token = tokens.issue(claims, path, 7, 100);
    LT_ASSERT(token);
    LT_ASSERT(EVP_set_default_properties(nullptr, "provider=task153_missing_provider") == 1);
    auto issued = tokens.issue(claims, path, 7, 100);
    auto verified = tokens.verify(*token, path, 7, claims.retry_source, 100);
    auto restored = EVP_set_default_properties(nullptr, "");
    LT_CHECK_EQ(restored, 1);
    LT_CHECK(!issued && !verified);
    bool rejected = false;
    try {
        hd::quic_address_token invalid(std::span(key).first(31));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    LT_CHECK(rejected);
LT_END_AUTO_TEST(provider_failure_does_not_issue_or_verify_and_key_sizes_are_strict)
LT_BEGIN_AUTO_TEST(token_suite, smallest_and_largest_claims_have_unambiguous_encodings)
    hd::quic_address_token tokens;
    auto path = token_path();
    for (std::size_t width : {std::size_t{1}, std::size_t{20}}) {
        hd::quic_token_claims claims{token_cid(1), token_cid(2)};
        claims.original_destination.size = claims.retry_source.size = width;
        auto token = tokens.issue(claims, path, 7, 100);
        LT_ASSERT(token);
        auto verified = tokens.verify(*token, path, 7, claims.retry_source, 100);
        LT_ASSERT(verified);
        LT_CHECK(verified->original_destination == claims.original_destination);
        LT_CHECK_EQ(token->size(), std::size_t{44} + width * 2);
    }
LT_END_AUTO_TEST(smallest_and_largest_claims_have_unambiguous_encodings)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
