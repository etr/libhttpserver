/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <httpserver/detail/quic_crypto.hpp>
#include "../support/http3_network_owner.hpp"
#include "../support/http3_semantic_routes.hpp"
#include "./quic_tls_peer.hpp"
#include "./quic_owner_peer.hpp"
#include "./quic_crypto_test_support.hpp"
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
httpserver::server::peer_policy policy;
auto root = httpserver::server::resource_budget::root({});
LT_ASSERT(httpserver::server::route_registry::create(root, routes).ok());
h3net::connection owner(registry.acquire()->select_default(), *facts, routes, executor, 1, policy);
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
httpserver::server::peer_policy policy;
auto root = httpserver::server::resource_budget::root({});
LT_ASSERT(httpserver::server::route_registry::create(root, routes).ok());
h3net::connection owner(registry.acquire()->select_default(), *facts, routes, executor, 1, policy);
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
httpserver::server::peer_policy policy;
auto root = httpserver::server::resource_budget::root({});
LT_ASSERT(httpserver::server::route_registry::create(root, routes).ok());
auto owner = std::make_unique<h3net::connection>(registry.acquire()->select_default(), *facts, routes, executor, 1, policy);
auto storage = owner->storage_budget();
owner->on_datagram(packet);
LT_CHECK(storage.in_use(httpserver::server::resource::quic_reassembly_bytes) > 0);
owner.reset();
executor.run_pending();
LT_CHECK_EQ(storage.in_use(httpserver::server::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(teardown_releases_storage_after_a_partially_received_handshake)
LT_BEGIN_AUTO_TEST(network_owner_suite, protected_rebinding_keeps_two_parked_streams_and_complete_head_peer_snapshots)
    owner_test::peer f;
    httpserver::resume_signal pause;
    std::vector<httpserver::net::peer_address> peers;
    LT_ASSERT(f.routes
                  .route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
                         [&](httpserver::exchange& x) -> httpserver::task<void> {
                             const auto snapshot = x.peer();
                             peers.push_back(snapshot);
                             if (x.head().head_fields.first("x-park")) co_await pause.wait();
                             LT_CHECK(x.peer() == snapshot);
                             x.start_response(httpserver::http::status::from_code(200), {});
                             const std::string body = "retained";
                             co_await x.writer().write(std::as_bytes(std::span(body)));
                             co_await x.writer().finish();
                         })
                  .ok());
    f.connect();
    auto parked = h3test::get();
    parked.push_back({"x-park", "yes"});
    auto head = h3test::headers(parked);
    for (auto id : {0u, 4u}) {
        const std::array<hd::quic_frame, 1> frames{hd::quic_stream_frame{id, 0, head, false}};
        f.send(frames, f.address);
    }
    auto third = h3test::headers(h3test::get());
    const auto prefix = third.size() - 1;
    const std::array<hd::quic_frame, 1> partial{hd::quic_stream_frame{8, 0, std::span(third).first(prefix), false}};
    f.send(partial, f.address);
    f.drain(10);
    LT_ASSERT_EQ(peers.size(), 2u);
    auto candidate = f.address;
    candidate.peer.port = 5000;
    const std::array<hd::quic_frame, 2> tail{hd::quic_stream_frame{8, prefix, std::span(third).last(1), true, true, true}, hd::quic_padding_frame{1100}};
    f.withheld_numbers.push_back(f.numbers[2]);
    f.send(tail, candidate);
    LT_CHECK_EQ(peers.size(), 2u);
    LT_ASSERT(f.server->failure().empty());
    bool validated = false;
    for (unsigned turn = 0; turn < 20 && !validated; ++turn)
        if (auto sent = f.prepare()) {
            f.server->emitted(true, f.now);
            std::array<std::byte, 1600> plain{};
            unsigned level = 0;
            std::uint64_t number = 0;
            auto frames = f.decode(*sent, plain, level, number);
            if (sent->peer == candidate) {
                LT_CHECK_EQ(sent->bytes.size(), 1200u);
                for (const auto& frame : frames) {
                    LT_CHECK(!std::holds_alternative<hd::quic_stream_frame>(frame));
                    LT_CHECK(!std::holds_alternative<hd::quic_ack_frame>(frame));
                    if (auto* challenge = std::get_if<hd::quic_path_frame>(&frame); challenge && !challenge->response) {
                        const std::array<hd::quic_frame, 2> answer{hd::quic_path_frame{true, challenge->data}, tail[0]};
                        f.send(answer, candidate);
                        validated = true;
                    }
                }
            } else {
                // Old-path probing may precede the candidate; accepting it must not supersede migration intent.
                f.accept(*sent, false);
            }
        }
    LT_ASSERT(validated);
    LT_ASSERT(f.server->failure().empty());
    LT_ASSERT_EQ(peers.size(), 3u);
    LT_CHECK(peers[0] == f.address.peer && peers[1] == f.address.peer);
    LT_CHECK(peers[2] == candidate.peer);
    f.address = candidate;
    pause.signal();
    f.executor.run_pending();
    f.drain();
    LT_ASSERT(f.server->failure().empty());
    for (auto id : {0u, 4u, 8u}) {
        LT_CHECK(f.fins[id]);
        LT_CHECK_EQ(h3test::decode_response(f.output[id]).body, "retained");
    }
LT_END_AUTO_TEST(protected_rebinding_keeps_two_parked_streams_and_complete_head_peer_snapshots)
LT_BEGIN_AUTO_TEST(network_owner_suite, complete_packet_cid_retirement_preserves_parked_streams_in_either_frame_order)
    for (bool reverse : {false, true}) {
        owner_test::peer f;
        httpserver::resume_signal pause;
        unsigned started = 0;
        LT_ASSERT(f.routes
                      .route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
                             [&](httpserver::exchange& x) -> httpserver::task<void> {
                                 ++started;
                                 co_await pause.wait();
                                 x.start_response(httpserver::http::status::from_code(200), {});
                                 const std::string body = "retained";
                                 co_await x.writer().write(std::as_bytes(std::span(body)));
                                 co_await x.writer().finish();
                             })
                      .ok());
        f.connect();
        auto head = h3test::headers(h3test::get());
        const std::array<hd::quic_frame, 2> requests{hd::quic_stream_frame{0, 0, head, true}, hd::quic_stream_frame{4, 0, head, true}};
        f.send(requests, f.address);
        f.drain(10);
        LT_ASSERT_EQ(started, 2u);
        std::array<std::byte, 1> retired{std::byte{90}}, surviving{std::byte{91}};
        std::array<std::byte, 16> retired_token{std::byte{90}}, surviving_token{std::byte{91}};
        std::array<hd::quic_frame, 2> frames{hd::quic_new_connection_id_frame{1, 0, retired, retired_token}, hd::quic_new_connection_id_frame{2, 2, surviving, surviving_token}};
        if (reverse) std::reverse(frames.begin(), frames.end());

        f.send(frames, f.address);

        LT_ASSERT(f.server->failure().empty());
        f.client_cid = surviving;
        pause.signal();
        f.executor.run_pending();
        f.drain();
        LT_ASSERT(f.server->failure().empty());
        LT_CHECK_EQ(started, 2u);
        LT_CHECK(f.fins[0] && f.fins[4]);
        LT_CHECK_EQ(h3test::decode_response(f.output[0]).body, "retained");
        LT_CHECK_EQ(h3test::decode_response(f.output[4]).body, "retained");
    }
LT_END_AUTO_TEST(complete_packet_cid_retirement_preserves_parked_streams_in_either_frame_order)
LT_BEGIN_AUTO_TEST(network_owner_suite, unauthenticated_and_replayed_changed_paths_never_redirect_application_packets)
    owner_test::peer f;
    f.connect();
    f.drain(10);
    auto candidate = f.address;
    candidate.peer.port = 6000;
    const std::array<hd::quic_frame, 1> ping{hd::quic_ping_frame{}};
    auto packet = f.packet(ping, f.address);
    f.server->on_datagram(packet);
    auto forwarded = std::make_shared<hd::io_datagram>(*packet);
    forwarded->peer = candidate;
    f.server->on_datagram(forwarded);
    auto spoof = f.packet(ping, candidate);
    spoof->bytes.back() ^= std::byte{1};
    f.server->on_datagram(spoof);
    LT_ASSERT(f.server->failure().empty());
    for (unsigned turn = 0; turn < 10; ++turn)
        if (auto sent = f.prepare()) {
            LT_CHECK(sent->peer == f.address);
            f.server->emitted(true, f.now);
            f.accept(*sent);
        }
LT_END_AUTO_TEST(unauthenticated_and_replayed_changed_paths_never_redirect_application_packets)
LT_BEGIN_AUTO_TEST(network_owner_suite, live_policy_discovery_and_activation_denial_withholds_handlers_and_candidate_acks)
    for (bool deny_at_discovery : {false, true}) {
        owner_test::peer f;
        unsigned calls = 0;
        LT_ASSERT(f.routes
                      .route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
                             [&](httpserver::exchange& x) -> httpserver::task<void> {
                                 ++calls;
                                 x.respond(httpserver::http::status::from_code(204), {});
                                 co_return;
                             })
                      .ok());
        f.connect();
        f.drain(10);
        auto candidate = f.address;
        candidate.peer.address = *httpserver::net::parse_address("127.0.0.2");
        candidate.peer.port = 7000;
        if (deny_at_discovery) {
            LT_ASSERT(f.policy.deny("127.0.0.2").ok());
        }
        auto head = h3test::headers(h3test::get());
        const std::array<hd::quic_frame, 2> data{hd::quic_stream_frame{0, 0, head, true}, hd::quic_padding_frame{1000}};
        f.withheld_numbers.push_back(f.numbers[2]);
        f.send(data, candidate);
        LT_CHECK_EQ(calls, 0u);
        LT_ASSERT(f.server->failure().empty());
        bool saw_probe = false;
        for (unsigned turn = 0; turn < 10; ++turn)
            if (auto sent = f.prepare()) {
                f.server->emitted(true, f.now);
                if (sent->peer == candidate) {
                    LT_CHECK(!deny_at_discovery);
                    saw_probe = true;
                    std::array<std::byte, 1600> plain{};
                    unsigned level;
                    std::uint64_t number;
                    auto frames = f.decode(*sent, plain, level, number);
                    LT_ASSERT(f.policy.deny("127.0.0.2").ok());
                    for (const auto& frame : frames) {
                        LT_CHECK(!std::holds_alternative<hd::quic_stream_frame>(frame) && !std::holds_alternative<hd::quic_ack_frame>(frame));
                        if (auto* challenge = std::get_if<hd::quic_path_frame>(&frame); challenge && !challenge->response) {
                            const std::array<hd::quic_frame, 2> answer{hd::quic_path_frame{true, challenge->data}, data[0]};
                            f.send(answer, candidate);
                        }
                    }
                } else {
                f.accept(*sent, false);
            }
            }
        LT_CHECK(saw_probe != deny_at_discovery);
        LT_CHECK_EQ(calls, 0u);
        LT_ASSERT(f.server->failure().empty());
        f.send(data, f.address);
        f.drain();
        LT_CHECK_EQ(calls, 1u);
        LT_CHECK(f.fins[0]);
    }
LT_END_AUTO_TEST(live_policy_discovery_and_activation_denial_withholds_handlers_and_candidate_acks)
LT_BEGIN_AUTO_TEST(network_owner_suite, advertised_aliases_dispatch_to_same_owner_retire_and_do_not_create_ownership_cycles)
    owner_test::peer f;
    f.connect();
    f.drain(20);
    LT_ASSERT(f.issued.contains(1));
    LT_ASSERT_EQ(f.issued[1].size(), f.server_cid.size());
    std::copy(f.issued[1].begin(), f.issued[1].end(), f.server_cid.begin());
    const std::array<hd::quic_frame, 1> ping{hd::quic_ping_frame{}};
    auto alias_packet = f.packet(ping, f.address);
    const auto before = f.server->stats().authenticated;
    LT_CHECK(f.admission->routes().dispatch(alias_packet).code == hd::datagram_dispatch_code::routed);
    f.executor.run_pending();
    LT_CHECK_EQ(f.server->stats().authenticated, before + 1);
    f.server_cid.fill({});
    f.server_cid[0] = std::byte{1};
    const std::array<hd::quic_frame, 1> retire{hd::quic_retire_connection_id_frame{1}};
    f.send(retire, f.address);
    LT_ASSERT(f.server->failure().empty());
    LT_CHECK(f.admission->routes().dispatch(alias_packet).code == hd::datagram_dispatch_code::retired);
    auto storage = f.server->storage_budget();
    std::weak_ptr<h3net::connection> weak = f.server;
    LT_CHECK(f.admission->routes().remove(*f.registration));
    f.server.reset();
    f.executor.run_pending();
    LT_CHECK(weak.expired());
    LT_CHECK_EQ(storage.in_use(httpserver::server::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(advertised_aliases_dispatch_to_same_owner_retire_and_do_not_create_ownership_cycles)
LT_BEGIN_AUTO_TEST(network_owner_suite, forwarded_fresh_packet_before_original_keeps_old_path_stream_progress)
    owner_test::peer f;
    unsigned calls = 0;
    LT_ASSERT(f.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello", [&](httpserver::exchange& x) -> httpserver::task<void> {
        ++calls; x.respond(httpserver::http::status::from_code(204), {}); co_return;
    }).ok());
    f.connect(); f.drain(10);
    auto head = h3test::headers(h3test::get());
    const std::array<hd::quic_frame, 2> data{hd::quic_stream_frame{0, 0, head, true}, hd::quic_padding_frame{1000}};
    auto original = f.packet(data, f.address);
    auto forwarded = std::make_shared<hd::io_datagram>(*original); forwarded->peer.peer.port = 6500;
    f.server->on_datagram(forwarded); LT_CHECK_EQ(calls, 0u);
    f.server->on_datagram(original); LT_CHECK_EQ(calls, 1u);
    const std::array<hd::quic_frame, 1> ping{hd::quic_ping_frame{}};
    f.send(ping, f.address);
    for (unsigned turn = 0; turn < 30; ++turn) if (auto sent = f.prepare()) {
        f.server->emitted(true, f.now);
        if (sent->peer == f.address) {
            f.accept(*sent);
        } else {
            std::array<std::byte, 1600> plain{}; unsigned level; std::uint64_t number;
            auto frames = f.decode(*sent, plain, level, number);
            for (const auto& frame : frames) {
                LT_CHECK(!std::holds_alternative<hd::quic_stream_frame>(frame));
            }
        }
    }
    LT_ASSERT(f.server->failure().empty()); LT_CHECK(f.fins[0]); LT_CHECK_EQ(calls, 1u);
LT_END_AUTO_TEST(forwarded_fresh_packet_before_original_keeps_old_path_stream_progress)
LT_BEGIN_AUTO_TEST(network_owner_suite, negotiated_ceiling_bounds_standalone_protected_pmtu_probes)
    owner_test::peer f(1328);
    f.connect();
    bool saw_probe = false;
    for (unsigned turn = 0; turn < 30; ++turn) if (auto sent = f.prepare()) {
        LT_CHECK(sent->bytes.size() <= 1328);
        f.server->emitted(true, f.now);
        if (sent->bytes.size() == 1328) {
            std::array<std::byte, 1600> plain{};
            unsigned level = 0;
            std::uint64_t number = 0;
            auto frames = f.decode(*sent, plain, level, number);
            LT_CHECK(level == 2);
            LT_CHECK(std::any_of(frames.begin(), frames.end(), [](const auto& frame) { return std::holds_alternative<hd::quic_ping_frame>(frame); }));
            saw_probe = true;
        }
        f.accept(*sent);
    }
    LT_CHECK(saw_probe && f.server->failure().empty());
LT_END_AUTO_TEST(negotiated_ceiling_bounds_standalone_protected_pmtu_probes)
LT_BEGIN_AUTO_TEST(network_owner_suite, peer_minimum_ceiling_prevents_large_protected_probes)
    owner_test::peer f(1200);
    f.connect();
    for (unsigned turn = 0; turn < 20; ++turn) if (auto sent = f.prepare()) {
        LT_CHECK(sent->bytes.size() <= 1200);
        f.server->emitted(true, f.now);
        f.accept(*sent);
    }
    LT_CHECK(f.server->failure().empty());
LT_END_AUTO_TEST(peer_minimum_ceiling_prevents_large_protected_probes)
LT_BEGIN_AUTO_TEST(network_owner_suite, protected_handshake_coalesces_ready_spaces_with_one_initial_minimum)
    owner_test::peer f;
    f.connect();
    LT_CHECK(f.coalesced_datagrams > 0 && f.server->failure().empty());
LT_END_AUTO_TEST(protected_handshake_coalesces_ready_spaces_with_one_initial_minimum)
LT_BEGIN_AUTO_TEST(network_owner_suite, failed_second_packet_protection_abandons_all_prepared_spaces)
    auto root = httpserver::server::resource_budget::root({});
    hd::quic_recovery recovery({}, root);
    hd::quic_transport_parameters params;
    hd::quic_flow_control flow(hd::quic_endpoint_role::server, params, params, 4, root);
    hd::quic_key_state keys;
    std::array<std::byte, 8> cid{std::byte{1}};
    LT_ASSERT(keys.install_initial(hd::quic_endpoint_role::server, cid) == hd::quic_crypto_code::ok);
    std::array<std::byte, 1200> one{}, two{}, output{}, scratch{};
    LT_ASSERT(recovery.retain_crypto(hd::quic_pn_space::initial, 0, std::span(one).first(10)));
    LT_ASSERT(recovery.retain_crypto(hd::quic_pn_space::handshake, 0, std::span(two).first(10)));
    LT_ASSERT(recovery.receive_packet(hd::quic_pn_space::initial, 0, true, {}));
    LT_ASSERT(recovery.begin_datagram());
    auto a = recovery.prepare_datagram_packet(hd::quic_pn_space::initial, one, {}, {600, 40}, flow);
    auto b = recovery.prepare_datagram_packet(hd::quic_pn_space::handshake, two, {}, {600, 40}, flow);
    LT_ASSERT(a && b);
    hd::quic_packet_write write;
    write.destination = cid;
    write.packet_number_width = 4;
    write.length_width = 2;
    write.packet_number = a.packet_number;
    write.payload = std::span(one).first(a.bytes);
    auto protected_initial = keys.protect_packet(hd::quic_key_level::initial, write, output, scratch);
    LT_CHECK(protected_initial.code == hd::quic_crypto_code::ok);
    write.kind = hd::quic_packet_kind::handshake;
    write.packet_number = b.packet_number;
    write.payload = std::span(two).first(b.bytes);
    auto missing_keys = keys.protect_packet(hd::quic_key_level::handshake, write, output, scratch);
    LT_CHECK(missing_keys.code != hd::quic_crypto_code::ok);
    LT_ASSERT(recovery.abandon_datagram());
    LT_CHECK(recovery.bytes_in_flight() == 0 && flow.sent() == 0 && recovery.ack_deadline(hd::quic_pn_space::initial));
    LT_ASSERT(recovery.begin_datagram());
    auto next = recovery.prepare_datagram_packet(hd::quic_pn_space::initial, one, {}, {1200, 40}, flow);
    LT_CHECK(next && next.packet_number > a.packet_number);
    LT_ASSERT(recovery.abandon_datagram());
LT_END_AUTO_TEST(failed_second_packet_protection_abandons_all_prepared_spaces)
LT_BEGIN_AUTO_TEST(network_owner_suite, peer_close_preempts_stream_work_retains_routes_and_expires_silently)
    owner_test::peer peer;
    peer.connect();
    LT_CHECK(!peer.server->has_keys(hd::quic_key_level::initial));
    LT_CHECK(!peer.server->has_keys(hd::quic_key_level::handshake));
    const std::array<hd::quic_frame, 2> close{hd::quic_ping_frame{}, hd::quic_close_frame{true, 42, 0, {}}};
    auto packet = peer.packet(close, peer.address);
    peer.server->on_datagram(packet);
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::draining);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
    LT_CHECK(!peer.server->has_keys(hd::quic_key_level::application));
    LT_CHECK(peer.admission->routes().dispatch(packet).code == hd::datagram_dispatch_code::routed);
    peer.executor.run_pending();
    LT_CHECK(!peer.server->prepare(peer.now));
    auto endpoint = peer.server->deadline();
    LT_ASSERT(endpoint);
    peer.server->tick(*endpoint - std::chrono::nanoseconds(1));
    LT_CHECK(peer.admission->routes().dispatch(packet).code == hd::datagram_dispatch_code::routed);
    peer.server->tick(*endpoint);
    peer.executor.run_pending();
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closed);
    LT_CHECK(!peer.server->deadline());
    LT_CHECK(peer.admission->routes().dispatch(packet).code == hd::datagram_dispatch_code::retired);
    peer.server->tick(*endpoint);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(peer_close_preempts_stream_work_retains_routes_and_expires_silently)
LT_BEGIN_AUTO_TEST(network_owner_suite, authenticated_close_on_candidate_path_preempts_payload_and_keeps_application_path)
    owner_test::peer peer; peer.connect();
    auto candidate = peer.address; candidate.peer.port = 4999;
    const std::array<hd::quic_frame, 2> frames{hd::quic_close_frame{true, 0, 0, {}}, hd::quic_stream_frame{0, 0, {}, true}};
    auto packet = peer.packet(frames, candidate); auto forged = std::make_shared<hd::io_datagram>(*packet);
    forged->bytes.back() ^= std::byte{1}; peer.server->on_datagram(forged);
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::active);
    peer.server->on_datagram(packet);
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::draining);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(!peer.server->has_keys(hd::quic_key_level::application)); LT_CHECK(peer.server->failure().empty());
    LT_CHECK(!peer.server->prepare(peer.now)); peer.server->on_datagram(packet);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u); LT_CHECK(!peer.server->prepare(peer.now));
LT_END_AUTO_TEST(authenticated_close_on_candidate_path_preempts_payload_and_keeps_application_path)
LT_BEGIN_AUTO_TEST(network_owner_suite, local_close_is_protected_bounded_and_survives_failed_send)
    owner_test::peer peer;
    peer.connect();
    std::array<std::byte, 500> reason;
    reason.fill(std::byte{7});
    peer.server->request_close({true, 42, 0, reason}, peer.now);
    reason.fill(std::byte{9});
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closing);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    auto terminal = peer.server->prepare(peer.now);
    LT_ASSERT(terminal);
    LT_ASSERT(peer.server->deadline());
    LT_CHECK(*peer.server->deadline() > peer.now);
    std::array<std::byte, 1600> plain{};
    unsigned level;
    std::uint64_t number;
    auto frames = peer.decode(*terminal, plain, level, number);
    LT_CHECK_EQ(level, 2u);
    LT_ASSERT(!frames.empty());
    auto* close = std::get_if<hd::quic_close_frame>(&frames.front());
    LT_ASSERT(close);
    LT_CHECK(close->application && close->error == 42);
    LT_CHECK_EQ(close->reason.size(), hd::quic_connection_lifecycle::max_reason);
    LT_CHECK(close->reason.front() == std::byte{7});
    LT_CHECK(std::all_of(frames.begin() + 1, frames.end(), [](const auto& f) { return std::holds_alternative<hd::quic_padding_frame>(f); }));
    peer.server->emitted(false, peer.now);
    auto retry = peer.server->prepare(peer.now);
    LT_ASSERT(retry);
    LT_CHECK(retry->bytes == terminal->bytes);
    peer.server->emitted(true, peer.now);
    LT_CHECK(!peer.server->prepare(peer.now));
    LT_CHECK(peer.server->has_keys(hd::quic_key_level::application));
    auto endpoint = peer.server->deadline();
    const std::array<hd::quic_frame, 1> received{hd::quic_ping_frame{}};
    peer.send(received, peer.address);
    LT_CHECK(!peer.server->prepare(peer.now));
    LT_ASSERT(endpoint);
    peer.server->tick(*endpoint);
    LT_CHECK(!peer.server->prepare(*endpoint));
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
LT_END_AUTO_TEST(local_close_is_protected_bounded_and_survives_failed_send)
LT_BEGIN_AUTO_TEST(network_owner_suite, early_application_close_converts_and_idle_expiry_is_silent)
    owner_test::peer peer;
    peer.deliver_crypto();
    const auto now = h3net::clock_type::now();
    peer.server->request_close({true, 42, 0, {}}, now);
    LT_CHECK(!peer.server->has_keys(hd::quic_key_level::application));
    auto terminal = peer.server->prepare(now);
    LT_ASSERT(terminal);
    std::array<std::byte, 1600> plain{};
    unsigned level;
    std::uint64_t number;
    auto envelope = hd::parse_quic_envelope(terminal->bytes, peer.client_cid.size());
    LT_ASSERT(envelope.code == hd::quic_codec_code::ok);
    h3net::transmission first{{envelope.value.packet.begin(), envelope.value.packet.end()}, terminal->peer};
    auto frames = peer.decode(first, plain, level, number);
    LT_CHECK_EQ(level, 0u);
    auto* close = std::get_if<hd::quic_close_frame>(&frames.front());
    LT_ASSERT(close);
    LT_CHECK(!close->application && close->error == 0x0c && close->reason.empty());
    peer.server->emitted(true, now);
    owner_test::peer idle;
    idle.connect();
    idle.server->tick(h3net::clock_type::now() + std::chrono::seconds(200));
    LT_CHECK(idle.server->state() == hd::quic_lifecycle_state::closed);
    LT_CHECK(idle.server->failure().empty());
    LT_CHECK_EQ(idle.server->close_notifications(), 1u);
    LT_CHECK(!idle.server->deadline());
LT_END_AUTO_TEST(early_application_close_converts_and_idle_expiry_is_silent)
LT_BEGIN_AUTO_TEST(network_owner_suite, stateless_reset_routes_whole_unknown_cid_datagram_and_stops_work)
    owner_test::peer peer;
    peer.connect();
    peer.drain(10);
    std::array<std::byte, 1> cid{std::byte{8}};
    std::array<std::byte, 16> token{std::byte{19}};
    const std::array<hd::quic_frame, 1> advertisement{hd::quic_new_connection_id_frame{1, 1, cid, token}};
    peer.send(advertisement, peer.address);
    peer.client_cid = cid;
    auto reset = std::make_shared<hd::io_datagram>();
    reset->bytes.resize(40, std::byte{0x40});
    std::copy(token.begin(), token.end(), reset->bytes.end() - 16);
    reset->peer = peer.address;
    reset->socket_id = 1;
    LT_CHECK(peer.admission->routes().dispatch(reset).code == hd::datagram_dispatch_code::unknown);
    auto sent = peer.prepare();
    LT_ASSERT(sent);
    peer.server->emitted(false, peer.now);
    LT_CHECK(peer.admission->routes().dispatch(reset).code == hd::datagram_dispatch_code::unknown);
    sent = peer.prepare();
    LT_ASSERT(sent);
    peer.server->emitted(true, peer.now);
    LT_CHECK(peer.admission->routes().dispatch(reset).code == hd::datagram_dispatch_code::routed);
    peer.executor.run_pending();
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::draining);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
    LT_CHECK(!peer.server->prepare(peer.now));
    auto endpoint = peer.server->deadline();
    LT_ASSERT(endpoint);
    peer.server->tick(*endpoint);
    LT_CHECK(peer.admission->routes().dispatch(reset).code == hd::datagram_dispatch_code::unknown);
LT_END_AUTO_TEST(stateless_reset_routes_whole_unknown_cid_datagram_and_stops_work)
LT_BEGIN_AUTO_TEST(network_owner_suite, terminal_claim_cancels_multiple_suspended_handlers_before_packet_streams)
    owner_test::peer peer;
    httpserver::resume_signal pause;
    unsigned started = 0, settled = 0;
    LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
        [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++started;
            co_await pause.wait();
            ++settled;
            co_await x.writer().finish();
        }).ok());
    peer.connect();
    auto head = h3test::headers(h3test::get());
    const std::array<hd::quic_frame, 2> requests{hd::quic_stream_frame{0, 0, head, true}, hd::quic_stream_frame{4, 0, head, true}};
    peer.send(requests, peer.address);
    LT_CHECK_EQ(started, 2u);
    const std::array<hd::quic_frame, 2> close{hd::quic_stream_frame{8, 0, head, true}, hd::quic_close_frame{true, 42, 0, {}}};
    peer.send(close, peer.address);
    peer.executor.run_pending();
    LT_CHECK_EQ(started, 2u);
    const auto callbacks = settled;
    pause.signal();
    peer.executor.run_pending();
    peer.send(requests, peer.address);
    peer.server->tick(h3net::clock_type::now());
    LT_CHECK_EQ(started, 2u);
    LT_CHECK_EQ(settled, callbacks);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(terminal_claim_cancels_multiple_suspended_handlers_before_packet_streams)
LT_BEGIN_AUTO_TEST(network_owner_suite, pending_udp_completion_keeps_ownership_until_terminal_retirement)
    for (auto outcome : {hd::quic_identity_emission::unsent, hd::quic_identity_emission::sent, hd::quic_identity_emission::ambiguous}) {
        owner_test::peer peer;
        peer.connect();
        const std::array<hd::quic_frame, 1> ping{hd::quic_ping_frame{}};
        peer.send(ping, peer.address);
        auto sent = peer.prepare();
        LT_ASSERT(sent);
        peer.server->request_close({false, 17, 0, {}}, peer.now);
        LT_CHECK_EQ(peer.server->close_notifications(), 1u);
        LT_CHECK(!peer.server->prepare(peer.now));
        peer.server->emitted(outcome, peer.now);
        auto terminal = peer.server->prepare(peer.now);
        LT_ASSERT(terminal);
        peer.server->emitted(true, peer.now);
        LT_CHECK_EQ(peer.server->close_notifications(), 1u);
        LT_CHECK(peer.server->failure().empty());
    }
LT_END_AUTO_TEST(pending_udp_completion_keeps_ownership_until_terminal_retirement)
LT_BEGIN_AUTO_TEST(network_owner_suite, draining_key_disposal_is_observable_and_repeated_ticks_do_not_release_twice)
    cleanse_receipt receipt;
    owner_test::peer peer(65527, cleanse_receipt::observe, &receipt);
    peer.connect();
    const auto before = receipt.releases;
    const std::array<hd::quic_frame, 1> close{hd::quic_close_frame{true, 42, 0, {}}};
    peer.send(close, peer.address);
    LT_CHECK(receipt.releases > before);
    LT_CHECK(receipt.clean);
    auto endpoint = peer.server->deadline();
    LT_ASSERT(endpoint);
    const auto releases = receipt.releases;
    peer.server->tick(*endpoint);
    peer.server->tick(*endpoint);
    peer.server.reset();
    peer.executor.run_pending();
    LT_CHECK_EQ(receipt.releases, releases);
LT_END_AUTO_TEST(draining_key_disposal_is_observable_and_repeated_ticks_do_not_release_twice)
LT_BEGIN_AUTO_TEST(network_owner_suite, close_cancels_pending_body_reads_and_response_production)
    for (unsigned mode : {0u, 1u}) {
        owner_test::peer peer;
        unsigned started = 0, after = 0;
        httpserver::resume_signal pause;
        LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::post), "/hello",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                ++started;
                if (mode == 0) {
                    x.admit_body({});
                    co_await x.body().collect(16);
                } else {
                    x.start_response(httpserver::http::status::from_code(200), {});
                    co_await pause.wait();
                }
                ++after;
                co_await x.writer().finish();
            }).ok());
        peer.connect();
        auto head = h3test::headers(h3test::get("POST"));
        const std::array<hd::quic_frame, 2> requests{hd::quic_stream_frame{0, 0, head, false}, hd::quic_stream_frame{4, 0, head, false}};
        peer.send(requests, peer.address);
        LT_CHECK_EQ(started, 2u);
        peer.server->request_close({true, 42, 0, {}}, peer.now);
        peer.executor.run_pending();
        pause.signal();
        peer.executor.run_pending();
        peer.server->tick(peer.now);
        LT_CHECK_EQ(after, 0u);
        LT_CHECK_EQ(peer.server->close_notifications(), 1u);
        LT_CHECK(peer.server->failure().empty());
    }
LT_END_AUTO_TEST(close_cancels_pending_body_reads_and_response_production)
LT_BEGIN_AUTO_TEST(network_owner_suite, closing_to_draining_preserves_endpoint_and_retained_aliases)
    owner_test::peer peer;
    peer.connect();
    peer.drain(10);
    LT_ASSERT(!peer.issued.empty());
    peer.server->request_close({true, 42, 0, {}}, peer.now);
    auto terminal = peer.server->prepare(peer.now);
    LT_ASSERT(terminal);
    peer.server->emitted(true, peer.now);
    auto endpoint = peer.server->deadline();
    const std::array<hd::quic_frame, 1> close{hd::quic_close_frame{true, 99, 0, {}}};
    peer.send(close, peer.address);
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::draining);
    LT_CHECK(peer.server->deadline() == endpoint);
    LT_CHECK(!peer.server->has_keys(hd::quic_key_level::application));
    auto alias = std::make_shared<hd::io_datagram>();
    alias->bytes.push_back(std::byte{0x40});
    alias->bytes.insert(alias->bytes.end(), peer.issued.begin()->second.begin(), peer.issued.begin()->second.end());
    alias->bytes.resize(40);
    alias->peer = peer.address;
    alias->socket_id = 1;
    LT_ASSERT(endpoint);
    peer.server->tick(*endpoint - std::chrono::nanoseconds(1));
    LT_CHECK(peer.admission->routes().dispatch(alias).code == hd::datagram_dispatch_code::routed);
    peer.server->tick(*endpoint);
    peer.executor.run_pending();
    LT_CHECK(peer.admission->routes().dispatch(alias).code == hd::datagram_dispatch_code::retired);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(closing_to_draining_preserves_endpoint_and_retained_aliases)
LT_BEGIN_AUTO_TEST(network_owner_suite, unresolved_send_destruction_never_claims_proven_unsent)
    owner_test::peer peer;
    peer.deliver_crypto();
    const auto now = h3net::clock_type::now();
    peer.server->tick(now);
    auto pending = peer.server->prepare(now);
    LT_ASSERT(pending);
    auto budget = peer.facts->budget;
    const auto debit = budget->debited();
    LT_CHECK(debit > 0);
    peer.admission->routes().remove(*peer.registration);
    peer.registration.reset();
    peer.server.reset();
    peer.executor.run_pending();
    LT_CHECK_EQ(budget->debited(), debit);
LT_END_AUTO_TEST(unresolved_send_destruction_never_claims_proven_unsent)
LT_BEGIN_AUTO_TEST(network_owner_suite, terminal_output_waits_for_amplification_capacity)
    owner_test::peer peer;
    peer.deliver_crypto();
    auto budget = peer.facts->budget;
    auto held = budget->reserve_send(budget->received() * 3);
    LT_ASSERT(held);
    const auto now = h3net::clock_type::now();
    peer.server->request_close({true, 42, 0, {}}, now);
    LT_CHECK(!peer.server->prepare(now));
    LT_ASSERT(peer.server->deadline());
    LT_CHECK(*peer.server->deadline() > now);
    budget->cancel_unsent(*held);
    auto terminal = peer.server->prepare(now);
    LT_ASSERT(terminal);
    LT_CHECK_EQ(terminal->bytes.size(), 1200u);
    peer.server->emitted(hd::quic_identity_emission::ambiguous, now);
    LT_CHECK_EQ(budget->debited(), 1200u);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
LT_END_AUTO_TEST(terminal_output_waits_for_amplification_capacity)
LT_BEGIN_AUTO_TEST(network_owner_suite, delayed_duplicate_completion_after_close_is_harmless)
    owner_test::peer peer;
    peer.connect();
    peer.server->request_close({}, peer.now);
    auto close = peer.server->prepare(peer.now);
    LT_ASSERT(close);
    peer.server->emitted(true, peer.now);
    const auto sent = peer.server->stats().emitted;
    peer.server->emitted(true, peer.now);
    auto endpoint = peer.server->deadline();
    LT_ASSERT(endpoint);
    peer.server->tick(*endpoint);
    peer.server->emitted(hd::quic_identity_emission::ambiguous, *endpoint);
    LT_CHECK_EQ(peer.server->stats().emitted, sent);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(delayed_duplicate_completion_after_close_is_harmless)
LT_BEGIN_AUTO_TEST(network_owner_suite, known_cid_reset_checks_complete_datagram_after_authentication_failure)
    owner_test::peer peer;
    peer.connect();
    peer.drain(10);
    std::array<std::byte, 1> cid{std::byte{8}};
    std::array<std::byte, 16> token{std::byte{19}};
    const std::array<hd::quic_frame, 1> advertisement{hd::quic_new_connection_id_frame{1, 1, cid, token}};
    peer.send(advertisement, peer.address);
    peer.client_cid = cid;
    auto sent = peer.prepare();
    LT_ASSERT(sent);
    peer.server->emitted(hd::quic_identity_emission::ambiguous, peer.now);
    auto reset = std::make_shared<hd::io_datagram>();
    reset->bytes.push_back(std::byte{0x40});
    reset->bytes.insert(reset->bytes.end(), peer.server_cid.begin(), peer.server_cid.end());
    reset->bytes.resize(80);
    std::copy(token.begin(), token.end(), reset->bytes.end() - 16);
    reset->peer = peer.address;
    reset->socket_id = 1;
    auto wrong = std::make_shared<hd::io_datagram>(*reset);
    wrong->bytes.back() ^= std::byte{1};
    LT_CHECK(peer.admission->routes().dispatch(wrong).code == hd::datagram_dispatch_code::routed);
    peer.executor.run_pending();
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::active);
    LT_CHECK(peer.admission->routes().dispatch(reset).code == hd::datagram_dispatch_code::routed);
    peer.executor.run_pending();
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::draining);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(known_cid_reset_checks_complete_datagram_after_authentication_failure)
LT_BEGIN_AUTO_TEST(network_owner_suite, terminal_retirement_releases_request_metadata_while_owner_retains_cids)
    owner_test::peer peer;
    httpserver::resume_signal pause;
    LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
        [&](httpserver::exchange&) -> httpserver::task<void> { co_await pause.wait(); }).ok());
    peer.connect();
    auto budget = peer.server->application_storage_budget();
    const auto baseline = budget.in_use(httpserver::server::resource::quic_reassembly_bytes);
    auto head = h3test::headers(h3test::get());
    const std::array<hd::quic_frame, 2> requests{hd::quic_stream_frame{0, 0, head, true}, hd::quic_stream_frame{4, 0, head, true}};
    peer.send(requests, peer.address);
    LT_CHECK(budget.in_use(httpserver::server::resource::quic_reassembly_bytes) > baseline);
    peer.server->request_close({}, peer.now);
    peer.executor.run_pending();
    LT_CHECK(budget.in_use(httpserver::server::resource::quic_reassembly_bytes) < baseline);
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closing);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    pause.signal();
    peer.executor.run_pending();
    LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(terminal_retirement_releases_request_metadata_while_owner_retains_cids)
LT_BEGIN_AUTO_TEST(network_owner_suite, handler_initiated_close_defers_storage_retirement_until_receive_turn_finishes)
    owner_test::peer peer;
    unsigned started = 0;
    LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
        [&](httpserver::exchange&) -> httpserver::task<void> {
            ++started;
            peer.server->request_close({}, h3net::clock_type::now());
            co_return;
        }).ok());
    peer.connect();
    auto head = h3test::headers(h3test::get());
    const std::array<hd::quic_frame, 2> requests{hd::quic_stream_frame{0, 0, head, true}, hd::quic_stream_frame{4, 0, head, true}};
    peer.send(requests, peer.address);
    peer.executor.run_pending();
    LT_CHECK_EQ(started, 1u);
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closing);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
    LT_CHECK(peer.server->prepare(h3net::clock_type::now()));
LT_END_AUTO_TEST(handler_initiated_close_defers_storage_retirement_until_receive_turn_finishes)
LT_BEGIN_AUTO_TEST(network_owner_suite, immediate_post_handshake_close_uses_confirmed_pto_ack_delay)
    owner_test::peer peer;
    for (unsigned turn = 0; turn < 100; ++turn) {
        peer.deliver_crypto();
        if (peer.server->connected()) break;  // Close before the next recovery timer turn.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        peer.now = h3net::clock_type::now();
        peer.server->tick(peer.now);
        if (auto sent = peer.server->prepare(peer.now)) {
            peer.server->emitted(true, peer.now);
            peer.accept(*sent);
        }
    }
    LT_ASSERT(peer.server->connected());
    const auto now = h3net::clock_type::now();
    peer.server->request_close({}, now);
    auto close = peer.server->prepare(now);
    LT_ASSERT(close);
    peer.server->emitted(true, now);
    auto endpoint = peer.server->deadline();
    LT_ASSERT(endpoint);
    LT_CHECK(*endpoint >= now + std::chrono::milliseconds(75));
LT_END_AUTO_TEST(immediate_post_handshake_close_uses_confirmed_pto_ack_delay)
LT_BEGIN_AUTO_TEST(network_owner_suite, reset_route_capacity_failure_retires_actual_send_before_local_close)
    owner_test::peer peer;
    peer.limits.max_routes = 2;
    peer.admission = std::make_unique<hd::quic_server_admission>(peer.limits);
    peer.connect();
    peer.drain(10);
    struct ignored_sink final : hd::datagram_sink {
        void on_datagram(std::shared_ptr<const hd::io_datagram>) noexcept override {}
    };
    auto ignored = std::make_shared<ignored_sink>();
    for (unsigned n : {99u, 100u}) {
        std::array<std::byte, 16> token{std::byte(n)};
        LT_ASSERT(peer.admission->routes().register_reset(token, peer.address, peer.listener.datagrams(), ignored));
    }
    std::array<std::byte, 1> cid{std::byte{8}};
    std::array<std::byte, 16> token{std::byte{19}};
    const std::array<hd::quic_frame, 1> advertisement{hd::quic_new_connection_id_frame{1, 1, cid, token}};
    peer.send(advertisement, peer.address);
    peer.client_cid = cid;
    std::optional<h3net::transmission> sent;
    for (unsigned turn = 0; turn < 20 && !sent; ++turn) sent = peer.prepare();
    LT_ASSERT(sent);
    const auto emitted = peer.server->stats().emitted;
    peer.server->emitted(true, peer.now);
    LT_CHECK_EQ(peer.server->stats().emitted, emitted + 1);
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closing);
    LT_CHECK_EQ(peer.server->close_notifications(), 1u);
    LT_CHECK(peer.server->failure().empty());
    auto terminal = peer.server->prepare(peer.now);
    LT_ASSERT(terminal);
    std::array<std::byte, 1600> plain{};
    unsigned level;
    std::uint64_t number;
    auto frames = peer.decode(*terminal, plain, level, number);
    auto* close = std::get_if<hd::quic_close_frame>(&frames.front());
    LT_ASSERT(close);
    LT_CHECK(!close->application && close->error == 0x01);
    peer.server->emitted(true, peer.now);
LT_END_AUTO_TEST(reset_route_capacity_failure_retires_actual_send_before_local_close)
LT_BEGIN_AUTO_TEST(network_owner_suite, drain_rejects_later_work_and_closes_after_accepted_fin_ack)
    owner_test::peer peer; httpserver::resume_signal pause; unsigned entered = 0, finished = 0;
    LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
        [&](httpserver::exchange& x) -> httpserver::task<void> {
            ++entered; co_await pause.wait(); ++finished; x.respond(httpserver::http::status::from_code(204), {});
        }).ok());
    peer.connect(); auto head = h3test::headers(h3test::get());
    const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{0, 0, head, true}};
    peer.send(request, peer.address); LT_CHECK_EQ(entered, 1U);
    auto deadline = h3net::clock_type::now() + std::chrono::seconds(5); httpserver::server::drain_ticket ticket;
    LT_ASSERT(peer.server->begin_drain(deadline, ticket).ok());
    const std::array<hd::quic_frame, 1> later{hd::quic_stream_frame{4, 0, head, true}};
    peer.send(later, peer.address); LT_CHECK_EQ(entered, 1U);
    for (unsigned n = 0; n < 20; ++n) {
        peer.now = h3net::clock_type::now(); peer.server->tick(peer.now);
        if (auto output = peer.server->prepare(peer.now)) {
            peer.server->emitted(true, peer.now); peer.accept(*output);
        }
    }
    LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::active);
    LT_CHECK(!peer.output[3].empty()); LT_CHECK_EQ(peer.server->close_notifications(), 0U);
    pause.signal(); peer.executor.run_pending();
    for (unsigned n = 0; n < 200 && peer.server->state() == hd::quic_lifecycle_state::active; ++n) {
        peer.now = h3net::clock_type::now(); peer.server->tick(peer.now);
        if (auto output = peer.server->prepare(peer.now)) {
            peer.server->emitted(true, peer.now); peer.accept(*output);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto closed = peer.server->state() == hd::quic_lifecycle_state::closing;
    if (!closed) peer.server->tick(deadline);
    httpserver::server::drain_result result; LT_ASSERT(ticket.wait(result).ok());
    LT_CHECK(result.status == httpserver::server::drain_status::completed); LT_CHECK(closed);
    LT_CHECK(peer.fins[0]); LT_CHECK_EQ(finished, 1U); LT_CHECK_EQ(peer.server->close_notifications(), 1U);
    LT_CHECK(peer.server->has_keys(hd::quic_key_level::application)); LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(drain_rejects_later_work_and_closes_after_accepted_fin_ack)
LT_BEGIN_AUTO_TEST(network_owner_suite, final_fin_ack_and_immediate_peer_close_have_distinct_drain_results)
    for (bool acknowledge_fin : {false, true}) {
        owner_test::peer peer;
        httpserver::resume_signal pause;
        unsigned entered = 0;
        LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
            [&](httpserver::exchange& x) -> httpserver::task<void> {
                ++entered; co_await pause.wait(); x.respond(httpserver::http::status::from_code(200), {});
            }).ok());
        peer.connect(); peer.drain(10);
        LT_ASSERT(!peer.issued.empty());
        auto head = h3test::headers(h3test::get());
        const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{0, 0, head, true}};
        peer.send(request, peer.address); LT_CHECK_EQ(entered, 1U);
        auto deadline = h3net::clock_type::now() + std::chrono::seconds(5);
        httpserver::server::drain_ticket ticket;
        LT_ASSERT(peer.server->begin_drain(deadline, ticket).ok());
        // Receive and acknowledge both GOAWAY stages while accepted work is held.
        peer.drain(20);
        hd::quic_cursor cursor(peer.output[3]);
        LT_CHECK_EQ(cursor.varint(), 0U);
        std::vector<std::uint64_t> ids;
        while (cursor.remaining()) {
            auto type = cursor.varint(), size = cursor.varint();
            auto bytes = cursor.take(size);
            if (type == 7) ids.push_back(hd::decode_quic_varint(bytes).value);
        }
        LT_ASSERT_EQ(ids.size(), 2U);
        LT_CHECK_EQ(ids[0], hd::k_quic_max_integer - 3); LT_CHECK_EQ(ids[1], 4U);
        const std::array<hd::quic_frame, 1> late{hd::quic_stream_frame{4, 0, head, true}};
        peer.send(late, peer.address); LT_CHECK_EQ(entered, 1U);
        bool rejected = false;
        for (unsigned turn = 0; turn < 20; ++turn) {
            if (auto output = peer.prepare()) {
                peer.server->emitted(true, peer.now);
                std::array<std::byte, 1600> plain{}; unsigned level; std::uint64_t number;
                for (auto frame : peer.decode(*output, plain, level, number))
                    if (auto reset = std::get_if<hd::quic_reset_stream_frame>(&frame))
                        rejected |= reset->stream == 4 && reset->error == 267;
                peer.accept(*output);
            }
        }
        LT_CHECK(rejected); pause.signal(); peer.executor.run_pending();
        std::optional<std::uint64_t> final_packet;
        for (unsigned turn = 0; turn < 100 && !final_packet; ++turn) {
            if (auto output = peer.prepare()) {
                peer.server->emitted(true, peer.now);
                std::array<std::byte, 1600> plain{}; unsigned level; std::uint64_t number;
                for (auto frame : peer.decode(*output, plain, level, number))
                    if (auto stream = std::get_if<hd::quic_stream_frame>(&frame); stream && stream->stream == 0 && stream->fin)
                        final_packet = number;
                peer.accept(*output, !final_packet);
            }
        }
        LT_ASSERT(final_packet); LT_CHECK(peer.fins[0]);
        LT_CHECK_EQ(h3test::decode_response(peer.output[0]).heads[0][0].value, "200");
        LT_CHECK(h3test::decode_response(peer.output[0]).body.empty());
        LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::active);
        if (acknowledge_fin) {
            const std::array<hd::quic_frame, 1> ack{hd::quic_ack_frame{*final_packet, 0, 0, 0, {}, {}}};
            peer.send(ack, peer.address); peer.server->tick(h3net::clock_type::now());
            LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closing);
            LT_CHECK(peer.server->has_keys(hd::quic_key_level::application));
        }
        const std::array<hd::quic_frame, 1> close{hd::quic_close_frame{true, 256, 0, {}}};
        peer.send(close, peer.address);
        LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::draining);
        LT_CHECK(!peer.server->has_keys(hd::quic_key_level::application));
        LT_CHECK(!peer.server->prepare(h3net::clock_type::now()));
        httpserver::server::drain_result result, again;
        LT_ASSERT(ticket.wait(result).ok());
        LT_CHECK(result.status == (acknowledge_fin ? httpserver::server::drain_status::completed : httpserver::server::drain_status::deadline_expired));
        LT_CHECK_EQ(result.remaining, acknowledge_fin ? 0U : 2U);
        auto endpoint = peer.server->deadline(); LT_ASSERT(endpoint);
        auto alias = std::make_shared<hd::io_datagram>();
        alias->bytes.push_back(std::byte{0x40});
        alias->bytes.insert(alias->bytes.end(), peer.issued.begin()->second.begin(), peer.issued.begin()->second.end());
        alias->bytes.resize(40); alias->peer = peer.address; alias->socket_id = 1;
        peer.server->tick(*endpoint - std::chrono::nanoseconds(1));
        LT_CHECK(peer.admission->routes().dispatch(alias).code == hd::datagram_dispatch_code::routed);
        peer.server->tick(*endpoint); peer.executor.run_pending();
        LT_CHECK(peer.admission->routes().dispatch(alias).code == hd::datagram_dispatch_code::retired);
        peer.server->tick(deadline); LT_ASSERT(ticket.wait(again).ok());
        LT_CHECK(again.status == result.status); LT_CHECK_EQ(again.remaining, result.remaining);
        LT_CHECK_EQ(peer.server->close_notifications(), 1U); LT_CHECK(peer.server->failure().empty());
    }
LT_END_AUTO_TEST(final_fin_ack_and_immediate_peer_close_have_distinct_drain_results)
LT_BEGIN_AUTO_TEST(network_owner_suite, drain_deadline_cancels_once_and_preserves_closing_cids)
    owner_test::peer peer; httpserver::resume_signal pause; httpserver::stop_token stop; unsigned resumed = 0;
    LT_ASSERT(peer.routes.route(httpserver::http::method::known(httpserver::http::method_id::get), "/hello",
        [&](httpserver::exchange& x) -> httpserver::task<void> {
            stop = x.cancellation(); co_await pause.wait(); ++resumed; x.respond(httpserver::http::status::from_code(204), {});
        }).ok());
    peer.connect(); auto head = h3test::headers(h3test::get());
    const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{0, 0, head, true}}; peer.send(request, peer.address);
    auto deadline = h3net::clock_type::now() + std::chrono::seconds(5); httpserver::server::drain_ticket ticket;
    LT_ASSERT(peer.server->begin_drain(deadline, ticket).ok());
    LT_ASSERT(peer.server->deadline()); LT_CHECK(*peer.server->deadline() <= deadline);
    peer.server->tick(deadline - std::chrono::nanoseconds(1)); LT_CHECK(!stop.stop_requested());
    peer.server->tick(deadline); LT_CHECK(stop.stop_requested()); LT_CHECK(peer.server->state() == hd::quic_lifecycle_state::closing);
    httpserver::server::drain_result result; LT_ASSERT(ticket.wait(result).ok());
    LT_CHECK(result.status == httpserver::server::drain_status::deadline_expired); LT_CHECK_EQ(result.remaining, 2U);
    LT_CHECK_EQ(peer.server->close_notifications(), 1U); LT_CHECK(peer.server->has_keys(hd::quic_key_level::application));
    peer.server->tick(deadline); pause.signal(); peer.executor.run_pending(); LT_CHECK_EQ(resumed, 0U);
    LT_CHECK_EQ(peer.server->close_notifications(), 1U); LT_CHECK(peer.server->failure().empty());
LT_END_AUTO_TEST(drain_deadline_cancels_once_and_preserves_closing_cids)
LT_BEGIN_AUTO_TEST(network_owner_suite, small_peer_window_receives_large_response_prefix_before_credit_resume)
    owner_test::peer f(65527, nullptr, nullptr, {}, false, 4096);
    unsigned started = 0, completed = 0;
    h3semantic::install(f.routes, [&](const char* kind, httpserver::exchange&, std::size_t) {
        if (std::string_view(kind) == "write_started") ++started;
        if (std::string_view(kind) == "write_completed") ++completed;
    });
    f.connect();
    f.drain(10);
    auto head = h3test::headers(h3test::get("GET", "/large"));
    const std::array<hd::quic_frame, 1> frames{hd::quic_stream_frame{0, 0, head, true}};
    f.send(frames, f.address);
    f.executor.run_pending();
    for (unsigned turn = 0; turn < 100 && f.output[0].size() < 4096; ++turn) {
        f.now = h3net::clock_type::now();
        f.server->tick(f.now);
        if (auto sent = f.server->prepare(f.now)) {
            f.server->emitted(true, f.now);
            f.accept(*sent);
        } else {
            auto due = f.server->deadline();
            LT_ASSERT(due && *due < f.now + std::chrono::milliseconds(100));
            std::this_thread::sleep_until(*due);
        }
    }
    LT_CHECK_EQ(f.output[0].size(), 4096u);
    LT_CHECK_EQ(started, completed + 1);
    LT_CHECK(!f.fins[0]);
LT_END_AUTO_TEST(small_peer_window_receives_large_response_prefix_before_credit_resume)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
