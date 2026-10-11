/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <httpserver/detail/fake_io_backend.hpp>
#include <httpserver/detail/quic_network_connection.hpp>
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
    explicit fixture(bool adopt = false) {
        if (!credentials.replace(quic_test::credentials()).ok() || !hs::route_registry::create(root, routes).ok()) throw std::runtime_error("native fixture setup");
        if (SSL_set1_groups_list(peer.ssl.get(), "X25519") != 1) throw std::runtime_error("native test group");
        const std::array<std::byte, 1> source{std::byte{7}};
        auto params = h3test::parameters(); params.initial_source_cid = source;
        std::array<std::byte, 1024> encoded{};
        const auto result = hd::encode_quic_transport_parameters(params, hd::quic_endpoint_role::client, encoded);
        if (SSL_set_quic_tls_transport_params(peer.ssl.get(), reinterpret_cast<const unsigned char*>(encoded.data()), result.consumed) != 1 || !peer.step()) throw std::runtime_error("native ClientHello");
        const std::array<std::byte, 8> destination{std::byte{1}};
        hd::quic_initial_keys keys; hd::derive_quic_initial_keys(destination, keys);
        std::array<std::byte, 1200> payload{}, scratch{};
        auto crypto = hd::encode_quic_frame(hd::quic_crypto_frame{0, peer.output[0]}, payload, {hd::quic_packet_kind::initial, hd::quic_endpoint_role::client});
        if (crypto.code != hd::quic_codec_code::ok) throw std::runtime_error("native Initial payload");
        hd::quic_packet_write write; write.destination = destination; write.source = source; write.payload = std::span(payload).first(1161); write.packet_number_width = 4; write.length_width = 2;
        packet = std::make_shared<hd::io_datagram>(); packet->bytes.resize(1200);
        auto sealed = hd::protect_quic_packet(keys.client, write, packet->bytes, scratch);
        if (sealed.code != hd::quic_crypto_code::ok) throw std::runtime_error("native Initial protection"); packet->bytes.resize(sealed.consumed);
        packet->peer.peer = {*httpserver::net::parse_address("127.0.0.1"), 4000}; packet->socket_id = 1;
        hd::quic_admission_limits limits; limits.require_retry = false; limits.cid_length = 8;
        admission = std::make_unique<hd::quic_server_admission>(limits);
        const auto pending = admission->receive(packet, 0); const auto seen = admission->inspect(pending.pending);
        if (!seen) throw std::runtime_error("native Initial admission"); facts = *seen;
        hd::quic_network_config config; config.data_bytes = config.critical_bytes = 393216;
        owner = std::make_shared<hd::quic_network_connection>(credentials.acquire()->select_default(), facts, routes, executor, 1, policy, root, config, hd::request_dispatch_context{}, hd::http3_native_context{});
        if (adopt) {
            auto promotion = admission->promote(pending.pending, io.datagrams(), owner->route_sink(), 0);
            if (!promotion) throw std::runtime_error("native promotion");
            owner->adopt_routes(admission->routes(), io.datagrams(), std::move(promotion->registration)); executor.run_pending();
        } else { admission->cancel(pending.pending); owner->on_datagram(packet); }
        owner->tick(std::chrono::steady_clock::now());
    }
    std::shared_ptr<hd::io_datagram> pings(unsigned count) {
        std::array<std::byte, 1200> payload{}, scratch{};
        std::fill_n(payload.begin(), count, std::byte{1});
        const std::array<std::byte, 8> destination{std::byte{1}};
        const std::array<std::byte, 1> source{std::byte{7}};
        hd::quic_initial_keys keys; hd::derive_quic_initial_keys(destination, keys);
        hd::quic_packet_write write; write.destination = destination; write.source = source; write.payload = std::span(payload).first(1161);
        write.packet_number = 1; write.packet_number_width = 4; write.length_width = 2;
        auto value = std::make_shared<hd::io_datagram>(*packet);
        auto sealed = hd::protect_quic_packet(keys.client, write, value->bytes, scratch);
        if (sealed.code != hd::quic_crypto_code::ok) throw std::runtime_error("native ping packet");
        value->bytes.resize(sealed.consumed); return value;
    }
    hd::quic_key_state client_keys;
    std::array<std::uint64_t, 3> next_number{1, 0, 0}, copied{}, delivered{};
    std::array<std::optional<std::uint64_t>, 3> largest;
    std::array<std::vector<std::byte>, 3> received_crypto;
    void install_client_keys() {
        const auto* cipher = SSL_get_current_cipher(peer.ssl.get());
        if (!cipher) return;
        const auto id = SSL_CIPHER_get_protocol_id(cipher);
        const auto suite = id == 0x1302 ? hd::quic_cipher_suite::aes_256_gcm_sha384 :
            id == 0x1303 ? hd::quic_cipher_suite::chacha20_poly1305_sha256 : hd::quic_cipher_suite::aes_128_gcm_sha256;
        for (unsigned level = 1; level < 3; ++level) for (unsigned direction = 0; direction < 2; ++direction) {
            const auto l = static_cast<hd::quic_key_level>(level); const auto d = static_cast<hd::quic_key_direction>(direction);
            if (!peer.secrets[level][direction].empty() && !client_keys.keys(l, d) &&
                client_keys.install_traffic_secret(l, d, suite, peer.secrets[level][direction]) != hd::quic_crypto_code::ok)
                throw std::runtime_error("native client key installation");
        }
    }
    std::shared_ptr<hd::io_datagram> protected_frames(std::span<const hd::quic_frame> frames, unsigned level = 2) {
        std::array<std::byte, 1200> payload{}, scratch{};
        const auto kind = level == 0 ? hd::quic_packet_kind::initial : level == 1 ? hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
        std::size_t count = 0;
        for (const auto& frame : frames) {
            const auto encoded = hd::encode_quic_frame(frame, std::span(payload).subspan(count), {kind, hd::quic_endpoint_role::client});
            if (encoded.code != hd::quic_codec_code::ok) throw std::runtime_error("native client frame");
            count += encoded.consumed;
        }
        const std::array<std::byte, 1> source{std::byte{7}};
        hd::quic_packet_write write; write.kind = kind; write.destination = std::span(facts.destination.bytes).first(facts.destination.size);
        if (level != 2) { write.source = source; write.length_width = 2; }
        write.packet_number = next_number[level]++; write.packet_number_width = 4;
        write.payload = std::span(payload).first(level == 0 ? 1161 : std::max(count, std::size_t{4}));
        auto result = std::make_shared<hd::io_datagram>(*packet);
        auto sealed = client_keys.protect_packet(static_cast<hd::quic_key_level>(level), write, result->bytes, scratch);
        if (sealed.code != hd::quic_crypto_code::ok) throw std::runtime_error("native client protection");
        result->bytes.resize(sealed.consumed); return result;
    }
    void consume_server(const hd::quic_transmission& transmission) {
        auto remaining = std::span(transmission.bytes);
        while (!remaining.empty()) {
            auto envelope = hd::parse_quic_envelope(remaining, 1);
            if (envelope.code != hd::quic_codec_code::ok) throw std::runtime_error("native server envelope");
            const unsigned level = envelope.value.kind == hd::quic_packet_kind::initial ? 0 : envelope.value.kind == hd::quic_packet_kind::handshake ? 1 : 2;
            std::array<std::byte, 1600> plain{}, scratch{};
            auto opened = client_keys.open_packet(static_cast<hd::quic_key_level>(level), envelope.value.packet, 1, largest[level], true, plain, scratch);
            if (opened.code != hd::quic_crypto_code::ok) throw std::runtime_error("native server authentication");
            largest[level] = std::max(opened.header.packet_number, largest[level].value_or(0));
            hd::quic_frame_cursor cursor;
            while (cursor.offset < opened.payload_size) {
                const auto frame = hd::next_quic_frame(std::span(plain).first(opened.payload_size), cursor, {envelope.value.kind, hd::quic_endpoint_role::server});
                if (frame.code != hd::quic_codec_code::ok) throw std::runtime_error("native server frame");
                if (const auto* crypto = std::get_if<hd::quic_crypto_frame>(&frame.value)) {
                    auto& bytes = received_crypto[level]; bytes.resize(std::max(bytes.size(), crypto->offset + crypto->data.size()));
                    std::copy(crypto->data.begin(), crypto->data.end(), bytes.begin() + crypto->offset);
                    peer.input[level].insert(peer.input[level].end(), bytes.begin() + delivered[level], bytes.end());
                    delivered[level] = bytes.size();
                    if (!peer.step()) throw std::runtime_error("native client TLS flight");
                    install_client_keys();
                }
            }
            if (level != 0 || !owner->connected()) {
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
            for (unsigned level = 0; level < 3; ++level) while (copied[level] < peer.output[level].size()) {
                const auto count = std::min<std::size_t>(900, peer.output[level].size() - copied[level]);
                const std::array<hd::quic_frame, 1> flight{hd::quic_crypto_frame{copied[level], std::span(peer.output[level]).subspan(copied[level], count)}};
                copied[level] += count; owner->on_datagram(protected_frames(flight, level));
            }
            if (owner->connected() && SSL_is_init_finished(peer.ssl.get())) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const auto now = std::chrono::steady_clock::now(); owner->tick(now);
            if (auto sent = owner->prepare(now)) { owner->emitted(true, now); consume_server(*sent); }
        }
        throw std::runtime_error("native protected handshake unfinished");
    }
    ~fixture() { owner.reset(); admission.reset(); executor.run_pending(); }
};
httpserver::task<void> send(hd::quic_network_connection& owner, hd::io_connection_owner& io, hd::fake_io_backend& backend, hd::quic_transmission packet) {
    hd::udp_send_operation operation(io, 1, packet.bytes, packet.peer); operation.submit(backend);
    const auto result = co_await operation;
    owner.emitted(result.code == http::outcome_code::ok ? hd::quic_identity_emission::sent : hd::quic_identity_emission::ambiguous, std::chrono::steady_clock::now());
}
}
LT_BEGIN_SUITE(native_quic_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(native_quic_suite)
LT_BEGIN_AUTO_TEST(native_quic_suite, admitted_initial_is_credited_once_and_proven_unsent_burns_packet_number)
    fixture f; const auto now = std::chrono::steady_clock::now();
    LT_CHECK_EQ(f.facts.budget->received(), f.packet->bytes.size());
    const auto first = f.owner->prepare(now); LT_ASSERT(first);
    LT_CHECK_EQ(f.facts.budget->debited(), first->bytes.size()); LT_CHECK(f.owner->pending());
    f.owner->emitted(hd::quic_identity_emission::unsent, now);
    LT_CHECK_EQ(f.facts.budget->debited(), 0u); LT_CHECK(!f.owner->pending());
    const auto second = f.owner->prepare(now); LT_ASSERT(second);
    LT_CHECK(first->bytes != second->bytes);
    f.owner->emitted(hd::quic_identity_emission::sent, now);
    LT_CHECK_EQ(f.facts.budget->debited(), second->bytes.size());
LT_END_AUTO_TEST(admitted_initial_is_credited_once_and_proven_unsent_burns_packet_number)
LT_BEGIN_AUTO_TEST(native_quic_suite, actual_operation_completion_settles_preparation_once_without_ambiguous_refund)
    for (const auto outcome : {http::outcome_code::ok, http::outcome_code::cancelled}) {
        fixture f; hd::fake_io_backend backend;
        const auto packet = f.owner->prepare(std::chrono::steady_clock::now()); LT_ASSERT(packet);
        const auto debit = f.facts.budget->debited();
        httpserver::spawn(f.executor, send(*f.owner, f.io, backend, *packet), [](httpserver::task_result<void>) {});
        f.executor.run_pending(); const auto ops = backend.pending_ops(); LT_ASSERT(ops.size() == 1);
        LT_CHECK(f.owner->pending()); LT_CHECK_EQ(f.facts.budget->debited(), debit);
        LT_CHECK(backend.complete(*ops[0], {outcome, packet->bytes.size()}));
        LT_CHECK(f.owner->pending()); f.executor.run_pending();
        LT_CHECK(!f.owner->pending()); LT_CHECK_EQ(f.facts.budget->debited(), debit);
        LT_CHECK(!backend.complete(*ops[0], {})); backend.close(); f.executor.run_pending();
    }
LT_END_AUTO_TEST(actual_operation_completion_settles_preparation_once_without_ambiguous_refund)
LT_BEGIN_AUTO_TEST(native_quic_suite, received_datagrams_wait_for_pending_send_and_overflow_closes_bounded_owner)
    fixture f; const auto now = std::chrono::steady_clock::now();
    auto output = f.owner->prepare(now); LT_ASSERT(output);
    const auto received = f.facts.budget->received();
    f.owner->on_datagram(std::make_shared<hd::io_datagram>(*f.packet)); LT_CHECK_EQ(f.facts.budget->received(), received);
    f.owner->emitted(hd::quic_identity_emission::sent, now); f.owner->tick(now);
    LT_CHECK_EQ(f.facts.budget->received(), received + f.packet->bytes.size());
    output = f.owner->prepare(now); if (!output) output = f.owner->prepare(now + std::chrono::milliseconds(20));
    LT_ASSERT(output);
    for (unsigned n = 0; n < 17; ++n) f.owner->on_datagram(std::make_shared<hd::io_datagram>(*f.packet));
    LT_CHECK(f.owner->terminal()); LT_CHECK(f.owner->pending());
    LT_CHECK(f.root.in_use(hs::resource::quic_reassembly_bytes) <= f.root.capacity(hs::resource::quic_reassembly_bytes));
    f.owner->emitted(hd::quic_identity_emission::ambiguous, now); f.owner->tick(now + std::chrono::minutes(1));
    LT_CHECK(f.owner->retired()); f.owner.reset(); LT_CHECK_EQ(f.root.in_use(hs::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(received_datagrams_wait_for_pending_send_and_overflow_closes_bounded_owner)
LT_BEGIN_AUTO_TEST(native_quic_suite, close_expiry_keeps_cid_routes_and_pending_storage_until_settlement)
    fixture f(true); const auto now = std::chrono::steady_clock::now();
    const auto packet = f.owner->prepare(now); LT_ASSERT(packet);
    f.owner->request_close({false, 1, 0, {}}, now); const auto expiry = f.owner->deadline(); LT_ASSERT(expiry);
    LT_CHECK(*expiry > now + std::chrono::milliseconds(100));
    f.owner->tick(*expiry - std::chrono::nanoseconds(1)); LT_CHECK(!f.owner->retired());
    LT_CHECK(f.admission->routes().dispatch(f.packet).code == hd::datagram_dispatch_code::routed); f.executor.run_pending();
    f.owner->tick(*expiry); LT_CHECK(!f.owner->retired()); LT_CHECK(f.owner->pending());
    f.owner->emitted(hd::quic_identity_emission::ambiguous, *expiry); LT_CHECK(f.owner->retired());
    LT_CHECK(f.admission->routes().dispatch(f.packet).code != hd::datagram_dispatch_code::routed);
    f.owner.reset(); LT_CHECK_EQ(f.root.in_use(hs::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(close_expiry_keeps_cid_routes_and_pending_storage_until_settlement)
LT_BEGIN_AUTO_TEST(native_quic_suite, authenticated_frame_flood_refuses_before_unbounded_frame_storage)
    fixture f;
    f.owner->on_datagram(f.pings(200));
    LT_CHECK(f.owner->terminal());
    f.owner->tick(std::chrono::steady_clock::now() + std::chrono::minutes(1));
    LT_CHECK(f.owner->retired());
LT_END_AUTO_TEST(authenticated_frame_flood_refuses_before_unbounded_frame_storage)
LT_BEGIN_AUTO_TEST(native_quic_suite, cid_registration_does_not_own_connection_or_form_a_retirement_cycle)
    fixture f(true); const auto weak = std::weak_ptr(f.owner);
    f.owner.reset(); LT_CHECK(weak.expired());
    if (auto held = weak.lock()) { const auto now = std::chrono::steady_clock::now(); held->request_close({false, 1, 0, {}}, now); held->tick(now + std::chrono::minutes(1)); }
    LT_CHECK_EQ(f.root.in_use(hs::resource::quic_reassembly_bytes), 0u);
LT_END_AUTO_TEST(cid_registration_does_not_own_connection_or_form_a_retirement_cycle)
LT_BEGIN_AUTO_TEST(native_quic_suite, authenticated_close_on_unvalidated_path_is_terminal_without_path_or_payload_effects)
    fixture f; f.connect();
    const std::array<hd::quic_frame, 2> frames{hd::quic_close_frame{true, 0, 0, {}}, hd::quic_stream_frame{0, 0, {}, true}};
    auto close = f.protected_frames(frames); close->peer.peer.port = 4999;
    auto forged = std::make_shared<hd::io_datagram>(*close); forged->bytes.back() ^= std::byte{1};
    f.owner->on_datagram(forged); LT_CHECK(!f.owner->terminal());
    const auto received = f.facts.budget->received(); const auto debit = f.facts.budget->debited();
    f.owner->on_datagram(close); LT_CHECK(f.owner->terminal());
    LT_CHECK_EQ(f.facts.budget->received(), received); LT_CHECK_EQ(f.facts.budget->debited(), debit);
    LT_CHECK(!f.owner->prepare(std::chrono::steady_clock::now()));
    f.owner->on_datagram(close); LT_CHECK(!f.owner->prepare(std::chrono::steady_clock::now()));
    const auto expiry = f.owner->deadline(); LT_ASSERT(expiry);
    f.owner->tick(*expiry); LT_CHECK(f.owner->retired());
LT_END_AUTO_TEST(authenticated_close_on_unvalidated_path_is_terminal_without_path_or_payload_effects)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
