/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <algorithm>
#include <chrono>
#include <memory>
#include <map>
#include <string>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <httpserver/detail/fake_io_backend.hpp>
#include <httpserver/detail/quic_network_connection.hpp>
#include <httpserver/detail/protocol_limits.hpp>
#include "./http3_request_fixture.hpp"
#include "./quic_tls_peer.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
namespace {
struct fixture {
    hs::resource_budget root = hs::resource_budget::root({});
    hs::route_registry routes;
    hs::peer_policy policy;
    httpserver::manual_executor executor;
    hd::io_connection_owner io{executor};
    hd::tls_credentials_registry credentials;
    quic_test::peer peer{quic_test::client_context(), "a.example", {"h3"}, nullptr, false};
    std::unique_ptr<hd::quic_server_admission> admission;
    std::shared_ptr<hd::io_datagram> packet;
    hd::quic_admission_facts facts;
    std::shared_ptr<hd::quic_network_connection> owner;
    explicit fixture(bool adopt = false, std::size_t data_bytes = 393216, const hd::resolved_protocol_plan* plan = nullptr) {
        if (plan)
            root = hs::resource_budget::root(plan->report.budgets);
        if (!credentials.replace(quic_test::credentials()).ok() || !hs::route_registry::create(root, routes).ok())
            throw std::runtime_error("native fixture setup");
        if (SSL_set1_groups_list(peer.ssl.get(), "X25519") != 1)
            throw std::runtime_error("native test group");
        const std::array<std::byte, 1> source{std::byte{7}};
        auto params = h3test::parameters();
        params.initial_source_cid = source;
        std::array<std::byte, 1024> encoded{};
        const auto result = hd::encode_quic_transport_parameters(params, hd::quic_endpoint_role::client, encoded);
        if (SSL_set_quic_tls_transport_params(peer.ssl.get(), reinterpret_cast<const unsigned char*>(encoded.data()), result.consumed) != 1 || !peer.step())
            throw std::runtime_error("native ClientHello");
        const std::array<std::byte, 8> destination{std::byte{1}};
        hd::quic_initial_keys keys;
        hd::derive_quic_initial_keys(destination, keys);
        std::array<std::byte, 1200> payload{}, scratch{};
        auto crypto = hd::encode_quic_frame(hd::quic_crypto_frame{0, peer.output[0]}, payload, {hd::quic_packet_kind::initial, hd::quic_endpoint_role::client});
        if (crypto.code != hd::quic_codec_code::ok)
            throw std::runtime_error("native Initial payload");
        hd::quic_packet_write write;
        write.destination = destination;
        write.source = source;
        write.payload = std::span(payload).first(1161);
        write.packet_number_width = 4;
        write.length_width = 2;
        packet = std::make_shared<hd::io_datagram>();
        packet->bytes.resize(1200);
        auto sealed = hd::protect_quic_packet(keys.client, write, packet->bytes, scratch);
        if (sealed.code != hd::quic_crypto_code::ok)
            throw std::runtime_error("native Initial protection");
        packet->bytes.resize(sealed.consumed);
        packet->peer.peer = {*httpserver::net::parse_address("127.0.0.1"), 4000};
        packet->socket_id = 1;
        hd::quic_admission_limits limits;
        limits.require_retry = false;
        limits.cid_length = 8;
        admission = std::make_unique<hd::quic_server_admission>(limits);
        const auto pending = admission->receive(packet, 0);
        const auto seen = admission->inspect(pending.pending);
        if (!seen)
            throw std::runtime_error("native Initial admission");
        facts = *seen;
        hd::quic_network_config config;
        config.data_bytes = data_bytes;
        config.critical_bytes = 393216;
        if (plan) {
            config.data_bytes = plan->data_bytes;
            config.critical_bytes = plan->critical_bytes;
            config.max_requests = plan->report.http3->max_concurrent_requests;
            config.max_stream_records = plan->report.quic->max_stream_records;
            config.request_limits = plan->http3;
        }
        owner = std::make_shared<hd::quic_network_connection>(credentials.acquire()->select_default(), facts, routes, executor, 1, policy, root, config, hd::request_dispatch_context{},
                                                              hd::http3_native_context{});
        if (adopt) {
            auto promotion = admission->promote(pending.pending, io.datagrams(), owner->route_sink(), 0);
            if (!promotion)
                throw std::runtime_error("native promotion");
            owner->adopt_routes(admission->routes(), io.datagrams(), std::move(promotion->registration));
            executor.run_pending();
        } else {
            admission->cancel(pending.pending);
            owner->on_datagram(packet);
        }
        owner->tick(std::chrono::steady_clock::now());
    }
    std::shared_ptr<hd::io_datagram> pings(unsigned count) {
        std::array<std::byte, 1200> payload{}, scratch{};
        std::fill_n(payload.begin(), count, std::byte{1});
        const std::array<std::byte, 8> destination{std::byte{1}};
        const std::array<std::byte, 1> source{std::byte{7}};
        hd::quic_initial_keys keys;
        hd::derive_quic_initial_keys(destination, keys);
        hd::quic_packet_write write;
        write.destination = destination;
        write.source = source;
        write.payload = std::span(payload).first(1161);
        write.packet_number = 1;
        write.packet_number_width = 4;
        write.length_width = 2;
        auto value = std::make_shared<hd::io_datagram>(*packet);
        auto sealed = hd::protect_quic_packet(keys.client, write, value->bytes, scratch);
        if (sealed.code != hd::quic_crypto_code::ok)
            throw std::runtime_error("native ping packet");
        value->bytes.resize(sealed.consumed);
        return value;
    }
    hd::quic_key_state client_keys;
    std::uint64_t peer_stream_limit = 16;
    bool acknowledge_application = false;
    unsigned response_fins = 0;
    std::optional<hd::quic_path_frame> challenge;
    std::array<std::byte, 8> challenge_bytes{};
    std::map<std::uint64_t, std::vector<std::byte>> received_streams;
    std::array<std::uint64_t, 3> next_number{1, 0, 0}, copied{}, delivered{};
    std::array<std::optional<std::uint64_t>, 3> largest;
    std::array<std::vector<std::byte>, 3> received_crypto;
    void install_client_keys() {
        const auto* cipher = SSL_get_current_cipher(peer.ssl.get());
        if (!cipher)
            return;
        const auto id = SSL_CIPHER_get_protocol_id(cipher);
        const auto suite = id == 0x1302 ? hd::quic_cipher_suite::aes_256_gcm_sha384 : id == 0x1303 ? hd::quic_cipher_suite::chacha20_poly1305_sha256 : hd::quic_cipher_suite::aes_128_gcm_sha256;
        for (unsigned level = 1; level < 3; ++level)
            for (unsigned direction = 0; direction < 2; ++direction) {
                const auto l = static_cast<hd::quic_key_level>(level);
                const auto d = static_cast<hd::quic_key_direction>(direction);
                if (!peer.secrets[level][direction].empty() && !client_keys.keys(l, d) && client_keys.install_traffic_secret(l, d, suite, peer.secrets[level][direction]) != hd::quic_crypto_code::ok)
                    throw std::runtime_error("native client key installation");
            }
    }
    std::shared_ptr<hd::io_datagram> protected_frames(std::span<const hd::quic_frame> frames, unsigned level = 2) {
        std::array<std::byte, 1200> payload{}, scratch{};
        const auto kind = level == 0 ? hd::quic_packet_kind::initial : level == 1 ? hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
        std::size_t count = 0;
        for (const auto& frame : frames) {
            const auto encoded = hd::encode_quic_frame(frame, std::span(payload).subspan(count), {kind, hd::quic_endpoint_role::client});
            if (encoded.code != hd::quic_codec_code::ok)
                throw std::runtime_error("native client frame");
            count += encoded.consumed;
        }
        const std::array<std::byte, 1> source{std::byte{7}};
        hd::quic_packet_write write;
        write.kind = kind;
        write.destination = std::span(facts.destination.bytes).first(facts.destination.size);
        if (level != 2) {
            write.source = source;
            write.length_width = 2;
        }
        write.packet_number = next_number[level]++;
        write.packet_number_width = 4;
        write.payload = std::span(payload).first(level == 0 ? 1161 : std::max(count, std::size_t{4}));
        auto result = std::make_shared<hd::io_datagram>(*packet);
        auto sealed = client_keys.protect_packet(static_cast<hd::quic_key_level>(level), write, result->bytes, scratch);
        if (sealed.code != hd::quic_crypto_code::ok)
            throw std::runtime_error("native client protection");
        result->bytes.resize(sealed.consumed);
        return result;
    }
    std::shared_ptr<hd::io_datagram> unsupported_packet(hd::quic_packet_kind kind) {
        const std::array<std::byte, 16> tag{};
        const std::array<std::byte, 4> payload{}, versions{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}};
        const std::array<std::byte, 1> source{std::byte{7}}, token{std::byte{1}};
        hd::quic_packet_write write;
        write.kind = kind;
        write.destination = std::span(facts.destination.bytes).first(facts.destination.size);
        write.source = source;
        if (kind == hd::quic_packet_kind::version_negotiation) {
            write.versions = versions;
        } else {
            write.tag = tag;
            if (kind == hd::quic_packet_kind::retry)
                write.token = token;
            else
                write.payload = payload;
        }
        auto result = std::make_shared<hd::io_datagram>(*packet);
        auto encoded = hd::encode_quic_packet(write, result->bytes);
        if (encoded.code != hd::quic_codec_code::ok)
            throw std::runtime_error("native unsupported packet");
        result->bytes.resize(encoded.consumed);
        result->peer.peer.port = 4999;
        return result;
    }
    bool ping_acknowledged() {
        const auto number = next_number[2];
        const std::array<hd::quic_frame, 1> ping{hd::quic_ping_frame{}};
        owner->on_datagram(protected_frames(ping));
        const auto now = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
        owner->tick(now);
        const auto output = owner->prepare(now);
        if (!output)
            return false;
        owner->emitted(true, now);
        auto remaining = std::span(output->bytes);
        while (!remaining.empty()) {
            const auto envelope = hd::parse_quic_envelope(remaining, 1);
            if (envelope.code != hd::quic_codec_code::ok)
                throw std::runtime_error("native continuation envelope");
            const unsigned level = envelope.value.kind == hd::quic_packet_kind::initial ? 0 : envelope.value.kind == hd::quic_packet_kind::handshake ? 1 : 2;
            std::array<std::byte, 1600> plain{}, scratch{};
            const auto opened = client_keys.open_packet(static_cast<hd::quic_key_level>(level), envelope.value.packet, 1, largest[level], true, plain, scratch);
            if (opened.code != hd::quic_crypto_code::ok)
                throw std::runtime_error("native continuation authentication");
            hd::quic_frame_cursor cursor;
            while (cursor.offset < opened.payload_size) {
                const auto frame = hd::next_quic_frame(std::span(plain).first(opened.payload_size), cursor, {envelope.value.kind, hd::quic_endpoint_role::server});
                if (frame.code != hd::quic_codec_code::ok)
                    throw std::runtime_error("native continuation frame");
                if (const auto* ack = std::get_if<hd::quic_ack_frame>(&frame.value); level == 2 && ack && ack->largest == number)
                    return true;
            }
            remaining = remaining.subspan(envelope.consumed);
        }
        return false;
    }
    std::size_t largest_datagram = 0;
    void consume_server(const hd::quic_transmission& transmission) {
        largest_datagram = std::max(largest_datagram, transmission.bytes.size());
        auto remaining = std::span(transmission.bytes);
        while (!remaining.empty()) {
            auto envelope = hd::parse_quic_envelope(remaining, 1);
            if (envelope.code != hd::quic_codec_code::ok)
                throw std::runtime_error("native server envelope");
            const unsigned level = envelope.value.kind == hd::quic_packet_kind::initial ? 0 : envelope.value.kind == hd::quic_packet_kind::handshake ? 1 : 2;
            std::array<std::byte, 1600> plain{}, scratch{};
            auto opened = client_keys.open_packet(static_cast<hd::quic_key_level>(level), envelope.value.packet, 1, largest[level], true, plain, scratch);
            if (opened.code != hd::quic_crypto_code::ok)
                throw std::runtime_error("native server authentication");
            largest[level] = std::max(opened.header.packet_number, largest[level].value_or(0));
            hd::quic_frame_cursor cursor;
            while (cursor.offset < opened.payload_size) {
                const auto frame = hd::next_quic_frame(std::span(plain).first(opened.payload_size), cursor, {envelope.value.kind, hd::quic_endpoint_role::server});
                if (frame.code != hd::quic_codec_code::ok)
                    throw std::runtime_error("native server frame");
                if (const auto* path = std::get_if<hd::quic_path_frame>(&frame.value); path && !path->response && transmission.peer.peer.port == 4999) {
                    std::copy(path->data.begin(), path->data.end(), challenge_bytes.begin());
                    challenge = hd::quic_path_frame{false, challenge_bytes};
                }
                if (const auto* credit = std::get_if<hd::quic_flow_frame>(&frame.value); credit && credit->kind == hd::quic_flow_kind::max_streams_bidi)
                    peer_stream_limit = std::max(peer_stream_limit, credit->limit);
                if (const auto* stream = std::get_if<hd::quic_stream_frame>(&frame.value); stream && !(stream->stream & 2) && stream->fin)
                    ++response_fins;
                if (const auto* stream = std::get_if<hd::quic_stream_frame>(&frame.value)) {
                    auto& bytes = received_streams[stream->stream];
                    if (stream->offset == bytes.size())
                        bytes.insert(bytes.end(), stream->data.begin(), stream->data.end());
                }
                if (const auto* crypto = std::get_if<hd::quic_crypto_frame>(&frame.value)) {
                    auto& bytes = received_crypto[level];
                    bytes.resize(std::max(bytes.size(), crypto->offset + crypto->data.size()));
                    std::copy(crypto->data.begin(), crypto->data.end(), bytes.begin() + crypto->offset);
                    peer.input[level].insert(peer.input[level].end(), bytes.begin() + delivered[level], bytes.end());
                    delivered[level] = bytes.size();
                    if (!peer.step())
                        throw std::runtime_error("native client TLS flight");
                    install_client_keys();
                }
            }
            if ((level != 0 || !owner->connected()) && (level != 2 || acknowledge_application)) {
                const std::array<hd::quic_frame, 1> ack{hd::quic_ack_frame{opened.header.packet_number, 0, 0, 0, {}, {}}};
                owner->on_datagram(protected_frames(ack, level));
            }
            remaining = remaining.subspan(envelope.consumed);
        }
    }
    void connect() {
        if (client_keys.install_initial(hd::quic_endpoint_role::client, std::span(facts.destination.bytes).first(facts.destination.size)) != hd::quic_crypto_code::ok)
            throw std::runtime_error("native client Initial keys");
        copied[0] = peer.output[0].size();
        for (unsigned turn = 0; turn < 100; ++turn) {
            install_client_keys();
            for (unsigned level = 0; level < 3; ++level)
                while (copied[level] < peer.output[level].size()) {
                    const auto count = std::min<std::size_t>(900, peer.output[level].size() - copied[level]);
                    const std::array<hd::quic_frame, 1> flight{hd::quic_crypto_frame{copied[level], std::span(peer.output[level]).subspan(copied[level], count)}};
                    copied[level] += count;
                    owner->on_datagram(protected_frames(flight, level));
                }
            if (owner->connected() && SSL_is_init_finished(peer.ssl.get()))
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const auto now = std::chrono::steady_clock::now();
            owner->tick(now);
            if (auto sent = owner->prepare(now)) {
                owner->emitted(true, now);
                consume_server(*sent);
            }
        }
        throw std::runtime_error("native protected handshake unfinished");
    }
    ~fixture() {
        owner.reset();
        admission.reset();
        executor.run_pending();
    }
};
httpserver::task<void> send(hd::quic_network_connection& owner, hd::io_connection_owner& io, hd::fake_io_backend& backend, hd::quic_transmission packet) {
    hd::udp_send_operation operation(io, 1, packet.bytes, packet.peer);
    operation.submit(backend);
    const auto result = co_await operation;
    owner.emitted(result.code == http::outcome_code::ok ? hd::quic_identity_emission::sent : hd::quic_identity_emission::ambiguous, std::chrono::steady_clock::now());
}
}  // namespace
LT_BEGIN_SUITE(native_quic_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(native_quic_suite)
LT_BEGIN_AUTO_TEST(native_quic_suite, admitted_initial_is_credited_once_and_proven_unsent_burns_packet_number)
fixture f;
const auto now = std::chrono::steady_clock::now();
LT_CHECK_EQ(f.facts.budget->received(), f.packet->bytes.size());
const auto first = f.owner->prepare(now);
LT_ASSERT(first);
LT_CHECK_EQ(f.facts.budget->debited(), first->bytes.size());
LT_CHECK(f.owner->pending());
f.owner->emitted(hd::quic_identity_emission::unsent, now);
LT_CHECK_EQ(f.facts.budget->debited(), 0u);
LT_CHECK(!f.owner->pending());
const auto second = f.owner->prepare(now);
LT_ASSERT(second);
LT_CHECK(first->bytes != second->bytes);
f.owner->emitted(hd::quic_identity_emission::sent, now);
LT_CHECK_EQ(f.facts.budget->debited(), second->bytes.size());
LT_END_AUTO_TEST(admitted_initial_is_credited_once_and_proven_unsent_burns_packet_number)
LT_BEGIN_AUTO_TEST(native_quic_suite, actual_operation_completion_settles_preparation_once_without_ambiguous_refund)
for (const auto outcome : {http::outcome_code::ok, http::outcome_code::cancelled}) {
    fixture f;
    hd::fake_io_backend backend;
    const auto packet = f.owner->prepare(std::chrono::steady_clock::now());
    LT_ASSERT(packet);
    const auto debit = f.facts.budget->debited();
    httpserver::spawn(f.executor, send(*f.owner, f.io, backend, *packet), [](httpserver::task_result<void>) {});
    f.executor.run_pending();
    const auto ops = backend.pending_ops();
    LT_ASSERT(ops.size() == 1);
    LT_CHECK(f.owner->pending());
    LT_CHECK_EQ(f.facts.budget->debited(), debit);
    LT_CHECK(backend.complete(*ops[0], {outcome, packet->bytes.size()}));
    LT_CHECK(f.owner->pending());
    f.executor.run_pending();
    LT_CHECK(!f.owner->pending());
    LT_CHECK_EQ(f.facts.budget->debited(), debit);
    LT_CHECK(!backend.complete(*ops[0], {}));
    backend.close();
    f.executor.run_pending();
}
LT_END_AUTO_TEST(actual_operation_completion_settles_preparation_once_without_ambiguous_refund)
LT_BEGIN_AUTO_TEST(native_quic_suite, unsupported_packets_for_established_cid_are_discarded_without_closing)
for (const auto kind : {hd::quic_packet_kind::zero_rtt, hd::quic_packet_kind::retry, hd::quic_packet_kind::version_negotiation}) {
    fixture f(true);
    f.connect();
    const auto forged = f.unsupported_packet(kind);
    const auto envelope = hd::parse_quic_envelope(forged->bytes);
    LT_ASSERT(envelope.code == hd::quic_codec_code::ok);
    LT_CHECK(envelope.value.kind == kind);
    const auto received = f.facts.budget->received();
    const auto debit = f.facts.budget->debited();
    LT_CHECK(f.admission->receive(forged, 0).code == hd::quic_admission_code::routed);
    f.executor.run_pending();
    LT_CHECK(!f.owner->terminal());
    LT_CHECK(f.owner->connected());
    LT_CHECK_EQ(f.facts.budget->received(), received);
    LT_CHECK_EQ(f.facts.budget->debited(), debit);
    LT_CHECK(f.ping_acknowledged());
}
LT_END_AUTO_TEST(unsupported_packets_for_established_cid_are_discarded_without_closing)
LT_BEGIN_AUTO_TEST(native_quic_suite, received_datagrams_wait_for_pending_send_before_accounting)
fixture f;
const auto now = std::chrono::steady_clock::now();
auto output = f.owner->prepare(now);
LT_ASSERT(output);
const auto received = f.facts.budget->received();
f.owner->on_datagram(std::make_shared<hd::io_datagram>(*f.packet));
LT_CHECK_EQ(f.facts.budget->received(), received);
f.owner->emitted(hd::quic_identity_emission::sent, now);
f.owner->tick(now);
LT_CHECK_EQ(f.facts.budget->received(), received + f.packet->bytes.size());
LT_END_AUTO_TEST(received_datagrams_wait_for_pending_send_before_accounting)
LT_BEGIN_AUTO_TEST(native_quic_suite, unauthenticated_queue_pressure_drops_excess_and_preserves_established_owner)
struct pressure_case {
    std::size_t data_bytes, packet_bytes, retained_packets;
};
// Record limit, exact byte limit, and storage reservation exhaustion.
for (const auto test : {pressure_case{393216, 64, 16}, pressure_case{393216, 16384, 2}, pressure_case{32768, 32768, 0}}) {
    fixture f(true, test.data_bytes);
    f.connect();
    const auto now = std::chrono::steady_clock::now();
    const auto output = f.owner->prepare(now);
    LT_ASSERT(output);
    const auto received = f.facts.budget->received();
    const auto debit = f.facts.budget->debited();
    const std::array<hd::quic_frame, 1> ping{hd::quic_ping_frame{}};
    auto forged = f.protected_frames(ping);
    forged->bytes.back() ^= std::byte{1};
    forged->bytes.resize(test.packet_bytes);
    forged->peer.peer.port = 4999;
    std::vector<std::weak_ptr<hd::io_datagram>> retained;
    for (std::size_t n = 0; n < test.retained_packets; ++n) {
        auto packet = std::make_shared<hd::io_datagram>(*forged);
        retained.push_back(packet);
        LT_CHECK(f.admission->receive(std::move(packet), 0).code == hd::quic_admission_code::routed);
        f.executor.run_pending();
        LT_CHECK(!retained.back().expired());
    }
    auto excess = std::make_shared<hd::io_datagram>(*forged);
    const auto dropped = std::weak_ptr(excess);
    LT_CHECK(f.admission->receive(std::move(excess), 0).code == hd::quic_admission_code::routed);
    f.executor.run_pending();
    LT_CHECK(dropped.expired());
    LT_CHECK(!f.owner->terminal());
    LT_CHECK(f.owner->pending());
    LT_CHECK_EQ(f.facts.budget->received(), received);
    LT_CHECK_EQ(f.facts.budget->debited(), debit);
    LT_CHECK(f.root.in_use(hs::resource::quic_reassembly_bytes) <= f.root.capacity(hs::resource::quic_reassembly_bytes));
    f.owner->emitted(hd::quic_identity_emission::ambiguous, now);
    f.owner->tick(now);
    LT_CHECK(!f.owner->pending());
    LT_CHECK(!f.owner->terminal());
    for (const auto& packet : retained)
        LT_CHECK(packet.expired());
    LT_CHECK(f.ping_acknowledged());
    f.owner.reset();
    LT_CHECK_EQ(f.root.in_use(hs::resource::quic_reassembly_bytes), 0u);
}
LT_END_AUTO_TEST(unauthenticated_queue_pressure_drops_excess_and_preserves_established_owner)
LT_BEGIN_AUTO_TEST(native_quic_suite, close_expiry_keeps_cid_routes_and_pending_storage_until_settlement)
fixture f(true);
const auto now = std::chrono::steady_clock::now();
const auto packet = f.owner->prepare(now);
LT_ASSERT(packet);
f.owner->request_close({false, 1, 0, {}}, now);
const auto expiry = f.owner->deadline();
LT_ASSERT(expiry);
LT_CHECK(*expiry > now + std::chrono::milliseconds(100));
f.owner->tick(*expiry - std::chrono::nanoseconds(1));
LT_CHECK(!f.owner->retired());
LT_CHECK(f.admission->routes().dispatch(f.packet).code == hd::datagram_dispatch_code::routed);
f.executor.run_pending();
f.owner->tick(*expiry);
LT_CHECK(!f.owner->retired());
LT_CHECK(f.owner->pending());
f.owner->emitted(hd::quic_identity_emission::ambiguous, *expiry);
LT_CHECK(f.owner->retired());
LT_CHECK(f.admission->routes().dispatch(f.packet).code != hd::datagram_dispatch_code::routed);
f.owner.reset();
LT_CHECK_EQ(f.root.in_use(hs::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(close_expiry_keeps_cid_routes_and_pending_storage_until_settlement)
LT_BEGIN_AUTO_TEST(native_quic_suite, authenticated_frame_flood_refuses_before_unbounded_frame_storage)
fixture f;
f.owner->on_datagram(f.pings(200));
LT_CHECK(f.owner->terminal());
f.owner->tick(std::chrono::steady_clock::now() + std::chrono::minutes(1));
LT_CHECK(f.owner->retired());
LT_END_AUTO_TEST(authenticated_frame_flood_refuses_before_unbounded_frame_storage)
LT_BEGIN_AUTO_TEST(native_quic_suite, authenticated_application_frame_flood_still_closes_established_owner)
fixture f(true);
f.connect();
std::array<hd::quic_frame, 129> frames;
std::fill(frames.begin(), frames.end(), hd::quic_ping_frame{});
LT_CHECK(f.admission->receive(f.protected_frames(frames), 0).code == hd::quic_admission_code::routed);
f.executor.run_pending();
LT_CHECK(f.owner->terminal());
f.owner->tick(std::chrono::steady_clock::now() + std::chrono::minutes(1));
LT_CHECK(f.owner->retired());
LT_END_AUTO_TEST(authenticated_application_frame_flood_still_closes_established_owner)
LT_BEGIN_AUTO_TEST(native_quic_suite, cid_registration_does_not_own_connection_or_form_a_retirement_cycle)
fixture f(true);
const auto weak = std::weak_ptr(f.owner);
f.owner.reset();
LT_CHECK(weak.expired());
if (auto held = weak.lock()) {
    const auto now = std::chrono::steady_clock::now();
    held->request_close({false, 1, 0, {}}, now);
    held->tick(now + std::chrono::minutes(1));
}
LT_CHECK_EQ(f.root.in_use(hs::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(cid_registration_does_not_own_connection_or_form_a_retirement_cycle)
LT_BEGIN_AUTO_TEST(native_quic_suite, authenticated_close_on_unvalidated_path_is_terminal_without_path_or_payload_effects)
fixture f;
f.connect();
const std::array<hd::quic_frame, 2> frames{hd::quic_close_frame{true, 0, 0, {}}, hd::quic_stream_frame{0, 0, {}, true}};
auto close = f.protected_frames(frames);
close->peer.peer.port = 4999;
auto forged = std::make_shared<hd::io_datagram>(*close);
forged->bytes.back() ^= std::byte{1};
f.owner->on_datagram(forged);
LT_CHECK(!f.owner->terminal());
const auto received = f.facts.budget->received();
const auto debit = f.facts.budget->debited();
f.owner->on_datagram(close);
LT_CHECK(f.owner->terminal());
LT_CHECK_EQ(f.facts.budget->received(), received);
LT_CHECK_EQ(f.facts.budget->debited(), debit);
LT_CHECK(!f.owner->prepare(std::chrono::steady_clock::now()));
f.owner->on_datagram(close);
LT_CHECK(!f.owner->prepare(std::chrono::steady_clock::now()));
const auto expiry = f.owner->deadline();
LT_ASSERT(expiry);
f.owner->tick(*expiry);
LT_CHECK(f.owner->retired());
LT_END_AUTO_TEST(authenticated_close_on_unvalidated_path_is_terminal_without_path_or_payload_effects)
LT_BEGIN_AUTO_TEST(native_quic_suite, acknowledged_terminal_requests_replenish_transport_stream_capacity)
fixture f;
unsigned dispatched = 0;
LT_ASSERT(f.routes
              .route(http::method::known(http::method_id::get), "/hello",
                     [&](httpserver::exchange& x) -> httpserver::task<void> {
                         ++dispatched;
                         x.respond(http::status::from_code(204), {});
                         co_return;
                     })
              .ok());
f.connect();
f.acknowledge_application = true;
const auto wire = h3test::headers(h3test::get());
for (unsigned n = 0; n < 24; ++n) {
    LT_ASSERT(!f.owner->terminal());
    LT_ASSERT(n < f.peer_stream_limit);
    const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{4 * n, 0, wire, true}};
    f.owner->on_datagram(f.protected_frames(request));
    f.executor.run_pending();
    for (unsigned turn = 0; turn < 100 && f.peer_stream_limit <= n + 16; ++turn) {
        const auto now = std::chrono::steady_clock::now();
        f.owner->tick(now);
        if (auto sent = f.owner->prepare(now)) {
            f.owner->emitted(true, now);
            f.consume_server(*sent);
        }
        f.executor.run_pending();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    LT_ASSERT(f.peer_stream_limit > n + 16);
}
LT_CHECK_EQ(dispatched, 24u);
LT_CHECK_EQ(f.response_fins, 24u);
LT_END_AUTO_TEST(acknowledged_terminal_requests_replenish_transport_stream_capacity)
LT_BEGIN_AUTO_TEST(native_quic_suite, retransmitted_terminal_stream_does_not_close_or_dispatch_again)
fixture f;
unsigned dispatched = 0;
LT_ASSERT(f.routes
              .route(http::method::known(http::method_id::get), "/hello",
                     [&](httpserver::exchange& x) -> httpserver::task<void> {
                         ++dispatched;
                         x.respond(http::status::from_code(204), {});
                         co_return;
                     })
              .ok());
f.connect();
f.acknowledge_application = true;
const auto wire = h3test::headers(h3test::get());
const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{0, 0, wire, true}};
f.owner->on_datagram(f.protected_frames(request));
f.executor.run_pending();
for (unsigned turn = 0; turn < 100 && f.peer_stream_limit == 16; ++turn) {
    const auto now = std::chrono::steady_clock::now();
    f.owner->tick(now);
    if (auto sent = f.owner->prepare(now)) {
        f.owner->emitted(true, now);
        f.consume_server(*sent);
    }
    f.executor.run_pending();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
LT_ASSERT(f.peer_stream_limit > 16);
f.owner->on_datagram(f.protected_frames(request));
f.executor.run_pending();
LT_CHECK(!f.owner->terminal());
LT_CHECK_EQ(dispatched, 1u);
const std::array<hd::quic_frame, 1> late_credit{hd::quic_flow_frame{hd::quic_flow_kind::max_stream_data, 131072, 0}};
f.owner->on_datagram(f.protected_frames(late_credit));
LT_CHECK(!f.owner->terminal());
LT_CHECK_EQ(dispatched, 1u);
LT_END_AUTO_TEST(retransmitted_terminal_stream_does_not_close_or_dispatch_again)
LT_BEGIN_AUTO_TEST(native_quic_suite, migrated_datagrams_do_not_credit_the_original_path_budget)
fixture f;
unsigned peer_port = 0;
LT_ASSERT(f.routes
              .route(http::method::known(http::method_id::get), "/hello",
                     [&](httpserver::exchange& x) -> httpserver::task<void> {
                         peer_port = x.peer().port;
                         x.respond(http::status::from_code(204), {});
                         co_return;
                     })
              .ok());
f.connect();
f.acknowledge_application = true;
const std::array<hd::quic_frame, 1> ping{hd::quic_ping_frame{}};
const std::array<hd::quic_frame, 2> discovery{hd::quic_ping_frame{}, hd::quic_padding_frame{1170}};
auto candidate = f.protected_frames(discovery);
candidate->peer.peer.port = 4999;
f.owner->on_datagram(candidate);
for (unsigned turn = 0; turn < 100 && !f.challenge; ++turn) {
    const auto now = std::chrono::steady_clock::now();
    f.owner->tick(now);
    if (auto output = f.owner->prepare(now)) {
        f.owner->emitted(true, now);
        f.consume_server(*output);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
LT_ASSERT(f.challenge);
f.challenge->response = true;
const std::array<hd::quic_frame, 1> validation{*f.challenge};
auto response = f.protected_frames(validation);
response->peer.peer.port = 4999;
f.owner->on_datagram(response);
f.owner->tick(std::chrono::steady_clock::now());
auto migrated = f.protected_frames(ping);
migrated->peer.peer.port = 4999;
const auto original = f.facts.budget->received();
f.owner->on_datagram(migrated);
const auto wire = h3test::headers(h3test::get());
const std::array<hd::quic_frame, 1> headers{hd::quic_stream_frame{0, 0, wire, true}};
auto request = f.protected_frames(headers);
request->peer.peer.port = 4999;
f.owner->on_datagram(request);
f.executor.run_pending();
LT_CHECK_EQ(peer_port, 4999u);
LT_CHECK_EQ(f.facts.budget->received(), original);
LT_CHECK(!f.owner->terminal());
LT_END_AUTO_TEST(migrated_datagrams_do_not_credit_the_original_path_budget)
LT_BEGIN_AUTO_TEST(native_quic_suite, delayed_drain_initiation_preserves_elapsed_server_deadline)
fixture f;
httpserver::resume_signal held;
unsigned entered = 0;
LT_ASSERT(f.routes
              .route(http::method::known(http::method_id::get), "/hello",
                     [&](httpserver::exchange&) -> httpserver::task<void> {
                         ++entered;
                         co_await held.wait();
                     })
              .ok());
f.connect();
const auto wire = h3test::headers(h3test::get());
const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{0, 0, wire, true}};
f.owner->on_datagram(f.protected_frames(request));
f.executor.run_pending();
LT_ASSERT(entered == 1u);
const auto now = std::chrono::steady_clock::now();
const auto drain_due = now + std::chrono::milliseconds(1);
f.owner->drain(drain_due);
LT_CHECK(f.owner->deadline() && *f.owner->deadline() <= drain_due);
f.owner->drain(now - std::chrono::milliseconds(1));
f.owner->tick(now);
LT_CHECK(f.owner->terminal());
held.signal();
f.executor.run_pending();
LT_END_AUTO_TEST(delayed_drain_initiation_preserves_elapsed_server_deadline)
LT_BEGIN_AUTO_TEST(native_quic_suite, native_settings_offer_bounded_dynamic_qpack)
fixture f;
f.connect();
f.acknowledge_application = true;
for (unsigned turn = 0; turn < 100 && f.received_streams[3].empty(); ++turn) {
    const auto now = std::chrono::steady_clock::now();
    f.owner->tick(now);
    if (auto sent = f.owner->prepare(now)) {
        f.owner->emitted(true, now);
        f.consume_server(*sent);
    }
}
auto wire = std::span(f.received_streams[3]);
const auto integer = [&]() {
    const auto decoded = hd::decode_quic_varint(wire);
    if (decoded.code != hd::quic_codec_code::ok)
        throw std::runtime_error("native SETTINGS integer");
    wire = wire.subspan(decoded.consumed);
    return decoded.value;
};
const auto role = integer(), type = integer();
LT_ASSERT_EQ(role, 0u);
LT_ASSERT_EQ(type, 4u);
const auto length = integer();
LT_ASSERT_EQ(wire.size(), length);
const auto capacity_key = integer(), capacity = integer();
const auto blocked_key = integer(), blocked = integer();
const auto connect_key = integer(), connect = integer();
LT_ASSERT_EQ(capacity_key, 1u);
LT_CHECK_EQ(capacity, 4096u);
LT_ASSERT_EQ(blocked_key, 7u);
LT_CHECK_EQ(blocked, 8u);
LT_ASSERT_EQ(connect_key, 8u);
LT_CHECK_EQ(connect, 1u);
LT_END_AUTO_TEST(native_settings_offer_bounded_dynamic_qpack)
LT_BEGIN_AUTO_TEST(native_quic_suite, resolved_growth_survives_eighty_requests_and_matches_wire_policy)
hs::server_options options;
options.add_listener({"127.0.0.1", 0, true, {}, hs::listener_transport::udp});
options.protocols().enable(http::protocol::http_3);
options.tls().provider = hs::tls_provider::system_default;
options.tls().profile = hs::tls_profile::certificates;
options.budgets().set(hs::resource::quic_reassembly_bytes, 16 * 1024 * 1024);
options.budgets().set(hs::resource::header_fields, 4096);
auto& h3 = options.http3().emplace();
h3.max_concurrent_requests = 24;
h3.decoder_table_bytes = 8192;
h3.encoder_table_bytes = 0;
h3.max_blocked_headers = 32;
h3.headers = {12000, 20000, 256};
options.quic().emplace().max_stream_records = 96;
hd::resolved_protocol_plan plan;
LT_ASSERT(hd::resolve_protocol_policy(options, plan).ok());
fixture f(false, 393216, &plan);
unsigned dispatched = 0;
LT_ASSERT(f.routes
              .route(http::method::known(http::method_id::get), "/hello",
                     [&](httpserver::exchange& x) -> httpserver::task<void> {
                         ++dispatched;
                         x.respond(http::status::from_code(204), {});
                         co_return;
                     })
              .ok());
f.connect();
f.acknowledge_application = true;
const auto parsed = hd::decode_quic_transport_parameters(f.peer.parameters, hd::quic_endpoint_role::server);
LT_ASSERT(parsed.code == hd::quic_codec_code::ok);
LT_CHECK_EQ(parsed.value.initial_max_streams_bidi, plan.report.quic->initial_max_bidirectional_streams);
LT_CHECK_EQ(parsed.value.initial_max_streams_uni, plan.report.quic->initial_max_unidirectional_streams);
LT_CHECK_EQ(parsed.value.max_udp_payload_size, plan.report.quic->advertised_max_udp_payload_bytes);
f.peer_stream_limit = parsed.value.initial_max_streams_bidi;
const auto control = h3test::bytes({0, 4, 0}), encoder = h3test::bytes({2}), decoder = h3test::bytes({3});
const std::array<hd::quic_frame, 3> peer_critical{hd::quic_stream_frame{2, 0, control, false}, hd::quic_stream_frame{6, 0, encoder, false}, hd::quic_stream_frame{10, 0, decoder, false}};
f.owner->on_datagram(f.protected_frames(peer_critical));
const auto wire = h3test::headers(h3test::get());
std::size_t opening_compressed = 0, opening_expanded = 0;
for (unsigned n = 0; n < 80; ++n) {
    LT_ASSERT(!f.owner->terminal());
    auto request_wire = wire;
    if (!n) {
        auto fields = h3test::get();
        for (unsigned field = 0; field < 180; ++field)
            fields.push_back({"x-growth-" + std::to_string(field), std::string(50, 'v')});
        const auto section = h3test::encode(fields);
        opening_compressed = section.size();
        request_wire = h3test::frame(1, section);
        for (const auto& field : fields) opening_expanded += field.name.size() + field.value.size() + 32;
        LT_CHECK(opening_compressed > 8192 && opening_compressed <= 12000);
        LT_CHECK(opening_expanded > 16384 && opening_expanded <= 20000);
    }
    for (std::size_t offset = 0; offset < request_wire.size();) {
        const auto count = std::min<std::size_t>(900, request_wire.size() - offset);
        const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{4 * n, offset, std::span(request_wire).subspan(offset, count), offset + count == request_wire.size(), true}};
        f.owner->on_datagram(f.protected_frames(request));
        f.executor.run_pending();
        offset += count;
    }
    for (unsigned turn = 0; turn < 100 && (f.peer_stream_limit <= n + 24 || f.received_streams[3].empty()); ++turn) {
        const auto now = std::chrono::steady_clock::now();
        f.owner->tick(now);
        if (auto sent = f.owner->prepare(now)) {
            f.owner->emitted(true, now);
            f.consume_server(*sent);
        }
        f.executor.run_pending();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    LT_ASSERT(f.peer_stream_limit > n + 24);
}
LT_CHECK_EQ(dispatched, 80u);
LT_CHECK_EQ(f.response_fins, 80u);
LT_CHECK(f.largest_datagram <= plan.report.quic->native_send_datagram_bytes);
auto settings_wire = std::span(f.received_streams[3]);
hd::quic_cursor cursor(settings_wire);
LT_CHECK_EQ(cursor.varint(), 0u);
LT_CHECK_EQ(cursor.varint(), 4u);
const auto length = cursor.varint();
LT_CHECK_EQ(length, cursor.remaining());
LT_CHECK_EQ(cursor.varint(), 1u);
LT_CHECK_EQ(cursor.varint(), 8192u);
LT_CHECK_EQ(cursor.varint(), 7u);
LT_CHECK_EQ(cursor.varint(), 32u);
LT_CHECK_EQ(cursor.varint(), 8u);
LT_CHECK_EQ(cursor.varint(), 1u);
LT_CHECK_EQ(cursor.remaining(), 0u);  // No MAX_FIELD_SECTION_SIZE.
std::cout << "CAPACITY_CAPTURE records=" << plan.report.quic->max_stream_records << " requests=" << dispatched << " initial_bidi=" << parsed.value.initial_max_streams_bidi
          << " initial_uni=" << parsed.value.initial_max_streams_uni << " max_udp=" << parsed.value.max_udp_payload_size << " replenished_credit=" << f.peer_stream_limit
          << " largest_datagram=" << f.largest_datagram << " compressed_headers=" << opening_compressed << " expanded_headers=" << opening_expanded << '\n';
// Fill the remaining ten request slots: six critical records plus90 terminal
// request records occupy the configured96-record retained ledger exactly.
for (unsigned n = 80; n < 90; ++n) {
    const std::array<hd::quic_frame, 1> request{hd::quic_stream_frame{4 * n, 0, wire, true}};
    f.owner->on_datagram(f.protected_frames(request));
    f.executor.run_pending();
    const auto expected_credit = std::min<std::uint64_t>(n + 25, 113);
    for (unsigned turn = 0; turn < 100 && (f.peer_stream_limit < expected_credit || f.response_fins < n + 1); ++turn) {
        const auto now = std::chrono::steady_clock::now();
        f.owner->tick(now);
        if (auto sent = f.owner->prepare(now)) {
            f.owner->emitted(true, now);
            f.consume_server(*sent);
        }
        f.executor.run_pending();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "LEDGER_FILL request=" << n << " credit=" << f.peer_stream_limit << " responses=" << f.response_fins << '\n';
    LT_ASSERT(!f.owner->terminal());
    LT_ASSERT_EQ(f.peer_stream_limit, expected_credit);
    LT_ASSERT_EQ(f.response_fins, n + 1);
}
LT_CHECK_EQ(dispatched, 90u);
LT_CHECK_EQ(f.response_fins, 90u);
const std::array<hd::quic_frame, 1> exhausted{hd::quic_stream_frame{360, 0, wire, true}};
f.owner->on_datagram(f.protected_frames(exhausted));
f.executor.run_pending();
const bool refused = f.owner->terminal();
LT_CHECK(refused);
LT_CHECK_EQ(dispatched, 90u);
f.owner.reset();
f.executor.run_pending();
LT_CHECK_EQ(f.root.in_use(hs::resource::quic_reassembly_bytes), 0u);
LT_CHECK_EQ(f.root.in_use(hs::resource::qpack_table_bytes), 0u);
std::cout << "LEDGER_CAPTURE critical=6 retired_requests=" << dispatched << " records=" << plan.report.quic->max_stream_records
          << " final_credit=" << f.peer_stream_limit << " next_request_refused=" << refused
          << " quic_storage=" << f.root.in_use(hs::resource::quic_reassembly_bytes) << " qpack_storage=" << f.root.in_use(hs::resource::qpack_table_bytes) << '\n';

LT_END_AUTO_TEST(resolved_growth_survives_eighty_requests_and_matches_wire_policy)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
