/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <algorithm>
#include <vector>
#include <memory>
#include <httpserver/detail/quic_crypto.hpp>
#include "../support/http3_network_owner.hpp"
#include "./quic_tls_peer.hpp"
#include "./littletest.hpp"
namespace {
std::shared_ptr<hd::io_datagram> initial(quic_test::peer& peer, std::uint64_t number = 0, std::size_t offset = 0) {
    std::array<std::byte, 8> cid{std::byte{1}}, source{std::byte{7}};
    std::vector<std::byte> payload(1200), scratch(1600);
    auto encoded = hd::encode_quic_frame(
        hd::quic_crypto_frame{offset, std::span(peer.output[0]).subspan(offset, std::min(std::size_t{900}, peer.output[0].size() - offset))}, payload,
        {hd::quic_packet_kind::initial, hd::quic_endpoint_role::client});
    if (encoded.code != hd::quic_codec_code::ok)
        throw std::runtime_error("encode failed");
    hd::quic_initial_keys keys;
    hd::derive_quic_initial_keys(cid, keys);
    hd::quic_packet_write write;
    write.destination = cid;
    write.source = std::span(source).first(1);
    write.payload = payload;
    write.packet_number = number;
    auto packet = std::make_shared<hd::io_datagram>();
    packet->bytes.resize(1600);
    auto protected_packet = hd::protect_quic_packet(keys.client, write, packet->bytes, scratch);
    if (protected_packet.code != hd::quic_crypto_code::ok)
        throw std::runtime_error("protect failed");
    packet->bytes.resize(protected_packet.consumed);
    packet->peer.peer.address = *httpserver::net::parse_address("127.0.0.1");
    packet->peer.peer.port = 4000;
    packet->socket_id = 1;
    return packet;
}
}  // namespace
LT_BEGIN_SUITE(network_owner_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(network_owner_suite)
LT_BEGIN_AUTO_TEST(network_owner_suite, authenticated_duplicate_does_not_repeat_crypto_or_credit_initial_twice)
hd::tls_credentials_registry registry;
LT_ASSERT(registry.replace(quic_test::credentials()).ok());
quic_test::peer peer(quic_test::client_context(), "a.example", {"h3"}, nullptr, false);
LT_ASSERT(peer.step());
auto packet = initial(peer);
hd::quic_admission_limits limits;
limits.require_retry = false;
hd::quic_server_admission admission(limits);
auto pending = admission.receive(packet, 0);
auto facts = admission.inspect(pending.pending);
LT_ASSERT(facts);
httpserver::manual_executor executor;
httpserver::server::route_registry routes;
auto root = httpserver::server::resource_budget::root({});
LT_ASSERT(httpserver::server::route_registry::create(root, routes).ok());
h3net::connection owner(registry.acquire()->select_default(), *facts, routes, executor, 1);
owner.on_datagram(packet);
LT_CHECK_EQ(owner.stats().authenticated, 1u);
LT_CHECK_EQ(facts->budget->received(), packet->bytes.size());
owner.on_datagram(std::make_shared<hd::io_datagram>(*packet));
LT_CHECK_EQ(owner.stats().duplicates, 1u);
LT_CHECK_EQ(owner.stats().authenticated, 1u);
LT_CHECK_EQ(facts->budget->received(), packet->bytes.size() * 2);
LT_END_AUTO_TEST(authenticated_duplicate_does_not_repeat_crypto_or_credit_initial_twice)
LT_BEGIN_AUTO_TEST(network_owner_suite, failed_atomic_send_releases_debit_and_reprotects_retained_crypto)
hd::tls_credentials_registry registry;
LT_ASSERT(registry.replace(quic_test::credentials()).ok());
quic_test::peer peer(quic_test::client_context(), "a.example", {"h3"}, nullptr, false);
LT_ASSERT(peer.step());
auto packet = initial(peer);
hd::quic_admission_limits limits;
limits.require_retry = false;
hd::quic_server_admission admission(limits);
auto pending = admission.receive(packet, 0);
auto facts = admission.inspect(pending.pending);
LT_ASSERT(facts);
httpserver::manual_executor executor;
httpserver::server::route_registry routes;
auto root = httpserver::server::resource_budget::root({});
LT_ASSERT(httpserver::server::route_registry::create(root, routes).ok());
h3net::connection owner(registry.acquire()->select_default(), *facts, routes, executor, 1);
owner.on_datagram(packet);
for (std::size_t offset = 900; offset < peer.output[0].size(); offset += 900)
    owner.on_datagram(initial(peer, offset / 900, offset));
auto now = h3net::clock_type::now();
owner.tick(now);
LT_CHECK(owner.failure().empty());
auto unsent = owner.prepare(now);
LT_ASSERT(unsent);
LT_CHECK_EQ(unsent->bytes.size(), 1200u);
LT_CHECK_EQ(owner.stats().emitted, 0u);
LT_CHECK_EQ(facts->budget->debited(), 1200u);
owner.emitted(false, now);
LT_CHECK_EQ(facts->budget->debited(), 0u);
LT_CHECK_EQ(owner.stats().emitted, 0u);
auto replacement = owner.prepare(now);
LT_ASSERT(replacement);
hd::quic_key_state keys;
const std::array<std::byte, 8> cid{std::byte{1}};
LT_CHECK(keys.install_initial(hd::quic_endpoint_role::client, cid) == hd::quic_crypto_code::ok);
std::array<std::byte, 1600> one{}, two{}, scratch{};
auto first = keys.open_packet(hd::quic_key_level::initial, unsent->bytes, 8, {}, false, one, scratch);
auto second = keys.open_packet(hd::quic_key_level::initial, replacement->bytes, 8, {}, false, two, scratch);
LT_CHECK(first.code == hd::quic_crypto_code::ok && second.code == hd::quic_crypto_code::ok);
LT_CHECK(second.header.packet_number > first.header.packet_number);
LT_CHECK(std::equal(one.begin(), one.begin() + first.payload_size, two.begin(), two.begin() + second.payload_size));
owner.emitted(true, now);
LT_CHECK_EQ(owner.stats().emitted, 1u);
LT_CHECK_EQ(facts->budget->debited(), 1200u);
LT_END_AUTO_TEST(failed_atomic_send_releases_debit_and_reprotects_retained_crypto)
LT_BEGIN_AUTO_TEST(network_owner_suite, teardown_releases_storage_after_a_partially_received_handshake)
hd::tls_credentials_registry registry;
LT_ASSERT(registry.replace(quic_test::credentials()).ok());
quic_test::peer peer(quic_test::client_context(), "a.example", {"h3"}, nullptr, false);
LT_ASSERT(peer.step());
auto packet = initial(peer);
hd::quic_admission_limits limits;
limits.require_retry = false;
hd::quic_server_admission admission(limits);
auto pending = admission.receive(packet, 0);
auto facts = admission.inspect(pending.pending);
LT_ASSERT(facts);
httpserver::manual_executor executor;
httpserver::server::route_registry routes;
auto root = httpserver::server::resource_budget::root({});
LT_ASSERT(httpserver::server::route_registry::create(root, routes).ok());
auto owner = std::make_unique<h3net::connection>(registry.acquire()->select_default(), *facts, routes, executor, 1);
auto storage = owner->storage_budget();
owner->on_datagram(packet);
LT_CHECK(storage.in_use(httpserver::server::resource::quic_reassembly_bytes) > 0);
owner.reset();
executor.run_pending();
LT_CHECK_EQ(storage.in_use(httpserver::server::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(teardown_releases_storage_after_a_partially_received_handshake)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
