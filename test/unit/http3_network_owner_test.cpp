/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <httpserver/detail/quic_crypto.hpp>
#include "../support/http3_network_owner.hpp"
#include "./quic_tls_peer.hpp"
#include "./quic_owner_peer.hpp"
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
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
