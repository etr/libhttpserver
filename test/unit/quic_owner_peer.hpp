/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_QUIC_OWNER_PEER_HPP_
#define TEST_UNIT_QUIC_OWNER_PEER_HPP_
#include <algorithm>
#include <memory>
#include <vector>
#include <map>
#include <thread>
#include <string>
#include "./http3_request_fixture.hpp"
#include "./quic_tls_peer.hpp"
#include "../support/http3_network_owner.hpp"
namespace owner_test {
inline void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}
struct peer {
    hd::tls_credentials_registry registry;
    quic_test::peer tls{quic_test::client_context(), "a.example", {"h3"}, nullptr, false};
    hd::quic_key_state keys;
    httpserver::manual_executor executor;
    hd::io_connection_owner listener{executor};
    httpserver::server::route_registry routes;
    httpserver::server::peer_policy policy;
    hd::quic_admission_limits limits;
    std::unique_ptr<hd::quic_server_admission> admission;
    std::shared_ptr<h3net::connection> server;
    std::array<std::byte, 8> server_cid{std::byte{1}};
    std::array<std::byte, 1> client_cid{std::byte{7}};
    std::size_t client_cid_width = 1;
    std::string file_root;
    std::array<std::uint64_t, 3> numbers{}, copied{}, delivered{};
    std::array<std::optional<std::uint64_t>, 3> largest;
    std::array<std::vector<std::byte>, 3> crypto;
    std::map<std::uint64_t, std::vector<std::byte>> output;
    std::map<std::uint64_t, bool> fins;
    std::vector<std::uint64_t> withheld_numbers;
    unsigned coalesced_datagrams = 0;
    hd::datagram_endpoint address;
    h3net::clock_type::time_point now = h3net::clock_type::now();
    std::optional<hd::quic_admission_facts> facts;
    std::optional<hd::quic_datagram_dispatch::cid_registration> registration;
    std::map<std::uint64_t, std::vector<std::byte>> issued;
    hd::quic_key_observer observer = nullptr;
    void* observer_argument = nullptr;
    explicit peer(std::uint64_t udp_ceiling = 65527, hd::quic_key_observer inspect_keys = nullptr, void* argument = nullptr,
                  const std::string& files = {}, bool empty_cid = false, std::optional<std::uint64_t> receive_window = {})
        : tls(quic_test::client_context(), "a.example", {files.empty() ? "h3" : "hq-interop"}, nullptr, false),
          client_cid_width(empty_cid ? 0 : 1), file_root(files), observer(inspect_keys), observer_argument(argument) {
        auto credentials = quic_test::credentials();
        if (!file_root.empty()) credentials.hosts[0].alpn = {"hq-interop"};
        require(registry.replace(credentials).ok(), "credentials");
        auto p = h3test::parameters();
        p.max_udp_payload_size = udp_ceiling;
        if (receive_window) p.initial_max_stream_data_bidi_local = *receive_window;
        p.initial_source_cid = std::span(client_cid).first(client_cid_width);
        p.active_connection_id_limit = 4;
        std::array<std::byte, 1024> bytes{};
        auto encoded = hd::encode_quic_transport_parameters(p, hd::quic_endpoint_role::client, bytes);
        require(encoded.code == hd::quic_codec_code::ok, "client parameters");
        require(SSL_set_quic_tls_transport_params(tls.ssl.get(), reinterpret_cast<const unsigned char*>(bytes.data()), encoded.consumed) == 1, "set client parameters");
        require(tls.step(), "clienthello");
        require(keys.install_initial(hd::quic_endpoint_role::client, server_cid) == hd::quic_crypto_code::ok, "initial keys");
        address.peer = {*httpserver::net::parse_address("127.0.0.1"), 4000};
        limits.require_retry = false;
        limits.cid_length = 8;
        admission = std::make_unique<hd::quic_server_admission>(limits);
        require(httpserver::server::route_registry::create(httpserver::server::resource_budget::root({}), routes).ok(), "routes");
    }
    ~peer() {
        if (registration) admission->routes().remove(*registration);
        server.reset();
        executor.run_pending();
    }
    void install_keys() {
        auto cipher = SSL_get_current_cipher(tls.ssl.get());
        if (!cipher) return;
        const auto id = SSL_CIPHER_get_protocol_id(cipher);
        const auto suite = id == 0x1302 ? hd::quic_cipher_suite::aes_256_gcm_sha384 : id == 0x1303 ? hd::quic_cipher_suite::chacha20_poly1305_sha256 : hd::quic_cipher_suite::aes_128_gcm_sha256;
        for (unsigned i = 1; i < 3; ++i)
            for (unsigned d = 0; d < 2; ++d) {
                auto level = static_cast<hd::quic_key_level>(i);
                auto direction = static_cast<hd::quic_key_direction>(d);
                if (!tls.secrets[i][d].empty() && !keys.keys(level, direction))
                    require(keys.install_traffic_secret(level, direction, suite, tls.secrets[i][d]) == hd::quic_crypto_code::ok, "traffic keys");
            }
    }
    std::shared_ptr<hd::io_datagram> packet(std::span<const hd::quic_frame> frames, hd::datagram_endpoint from, unsigned level = 2) {
        std::array<std::byte, 1200> plain{}, scratch{};
        std::size_t at = 0;
        const auto kind = level == 0 ? hd::quic_packet_kind::initial : level == 1 ? hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
        for (const auto& frame : frames) {
            auto encoded = hd::encode_quic_frame(frame, std::span(plain).subspan(at), {kind, hd::quic_endpoint_role::client});
            require(encoded.code == hd::quic_codec_code::ok, "encode client frame");
            at += encoded.consumed;
        }
        hd::quic_packet_write write;
        write.kind = kind;
        write.destination = server_cid;
        if (level != 2) write.source = std::span(client_cid).first(client_cid_width);
        write.packet_number = numbers[level]++;
        write.packet_number_width = 4;
        if (level != 2) write.length_width = 2;
        write.payload = std::span(plain).first(level == 0 ? 1162 - client_cid_width : std::max(at, std::size_t{4}));
        auto result = std::make_shared<hd::io_datagram>();
        result->peer = from;
        result->socket_id = 1;
        result->bytes.resize(1200);
        auto encoded = keys.protect_packet(static_cast<hd::quic_key_level>(level), write, result->bytes, scratch);
        require(encoded.code == hd::quic_crypto_code::ok, "protect client packet");
        result->bytes.resize(encoded.consumed);
        return result;
    }
    void send(std::span<const hd::quic_frame> frames, hd::datagram_endpoint from, unsigned level = 2) { server->on_datagram(packet(frames, from, level)); }
    void deliver_crypto() {
        install_keys();
        for (unsigned i = 0; i < 3; ++i) {
            while (copied[i] < tls.output[i].size()) {
                const auto count = std::min<std::size_t>(900, tls.output[i].size() - copied[i]);
                const std::array<hd::quic_frame, 1> frames{hd::quic_crypto_frame{copied[i], std::span(tls.output[i]).subspan(copied[i], count)}};
                auto bytes = packet(frames, address, i);
                copied[i] += count;
                if (!server) {
                    auto pending = admission->receive(bytes, 0);
                    facts = admission->inspect(pending.pending);
                    require(static_cast<bool>(facts), "Initial admission");
                    server = std::make_shared<h3net::connection>(registry.acquire()->select_default(), *facts, routes, executor, 77, policy, file_root, observer, observer_argument);
                    registration = admission->routes().register_cid(facts->destination, listener.datagrams(), server);
                    require(static_cast<bool>(registration), "initial route");
                    server->adopt_routes(admission->routes(), listener.datagrams(), *registration, server);
                }
                server->on_datagram(bytes);
            }
        }
    }
    std::vector<hd::quic_frame> decode(const h3net::transmission& sent, std::array<std::byte, 1600>& plain, unsigned& level, std::uint64_t& number) {
        auto envelope = hd::parse_quic_envelope(sent.bytes, client_cid_width);
        require(envelope.code == hd::quic_codec_code::ok, "server envelope");
        level = envelope.value.kind == hd::quic_packet_kind::initial ? 0 : envelope.value.kind == hd::quic_packet_kind::handshake ? 1 : 2;
        install_keys();
        std::array<std::byte, 1600> scratch{};
        auto opened = keys.open_packet(static_cast<hd::quic_key_level>(level), sent.bytes, client_cid_width, largest[level], true, plain, scratch);
        require(opened.code == hd::quic_crypto_code::ok, "open server packet");
        number = opened.header.packet_number;
        largest[level] = std::max(number, largest[level].value_or(0));
        hd::quic_frame_cursor cursor;
        std::vector<hd::quic_frame> frames;
        while (cursor.offset < opened.payload_size) {
            auto frame = hd::next_quic_frame(std::span(plain).first(opened.payload_size), cursor, {envelope.value.kind, hd::quic_endpoint_role::server});
            require(frame.code == hd::quic_codec_code::ok, "decode server frame");
            frames.push_back(frame.value);
        }
        return frames;
    }
    void accept_packet(const h3net::transmission& sent, bool acknowledge) {
        std::array<std::byte, 1600> plain{};
        unsigned level = 0;
        std::uint64_t number = 0;
        auto frames = decode(sent, plain, level, number);
        for (const auto& frame : frames) {
            if (auto* ack = std::get_if<hd::quic_ack_frame>(&frame); ack && level == 2) {
                hd::quic_ack_cursor cursor;
                for (std::size_t i = 0; i <= ack->range_count; ++i) {
                    auto range = hd::next_quic_ack_range(*ack, cursor);
                    require(range.code == hd::quic_codec_code::ok, "ACK range");
                    for (auto n : withheld_numbers) require(n < range.value.smallest || n > range.value.largest, "withheld STREAM packet acknowledged");
                }
            }
            if (auto* f = std::get_if<hd::quic_crypto_frame>(&frame)) {
                auto& bytes = crypto[level];
                bytes.resize(std::max<std::size_t>(bytes.size(), f->offset + f->data.size()));
                std::copy(f->data.begin(), f->data.end(), bytes.begin() + f->offset);
                if (bytes.size() > delivered[level]) {
                    tls.input[level].insert(tls.input[level].end(), bytes.begin() + delivered[level], bytes.end());
                    delivered[level] = bytes.size();
                    require(tls.step(), "client TLS receive");
                    install_keys();
                }
            }
            if (auto* f = std::get_if<hd::quic_new_connection_id_frame>(&frame)) issued[f->sequence] = {f->cid.begin(), f->cid.end()};
            if (auto* f = std::get_if<hd::quic_stream_frame>(&frame)) {
                auto& bytes = output[f->stream];
                bytes.resize(std::max<std::size_t>(bytes.size(), f->offset + f->data.size()));
                std::copy(f->data.begin(), f->data.end(), bytes.begin() + f->offset);
                fins[f->stream] |= f->fin;
            }
        }
        if (acknowledge && (level != 0 || !server->connected())) {
            const std::array<hd::quic_frame, 1> ack{hd::quic_ack_frame{number, 0, 0, 0, {}, {}}};
            send(ack, address, level);
        }
    }
    void accept(const h3net::transmission& sent, bool acknowledge = true) {
        auto bytes = std::span(sent.bytes);
        unsigned packets = 0;
        while (!bytes.empty()) {
            auto envelope = hd::parse_quic_envelope(bytes, client_cid_width);
            require(envelope.code == hd::quic_codec_code::ok, "coalesced envelope");
            h3net::transmission one{{bytes.begin(), bytes.begin() + envelope.consumed}, sent.peer};
            ++packets;
            accept_packet(one, acknowledge);
            bytes = bytes.subspan(envelope.consumed);
        }
        if (packets > 1) {
            require(sent.bytes.size() <= 1472, "coalesced handshake datagram ceiling");
            ++coalesced_datagrams;
        }
    }
    void connect() {
        for (unsigned turn = 0; turn < 100; ++turn) {
            deliver_crypto();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            now = h3net::clock_type::now();
            server->tick(now);
            require(server->failure().empty(), server->failure().c_str());
            if (auto sent = server->prepare(now)) {
                server->emitted(true, now);
                accept(*sent);
            }
            if (server->connected() && SSL_is_init_finished(tls.ssl.get())) {
                deliver_crypto();
                return;
            }
        }
        throw std::runtime_error("protected handshake did not finish");
    }
    std::optional<h3net::transmission> prepare() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        now = h3net::clock_type::now();
        server->tick(now);
        return server->prepare(now);
    }
    void drain(unsigned turns = 50) {
        for (unsigned i = 0; i < turns; ++i)
            if (auto sent = prepare()) {
                server->emitted(true, now);
                accept(*sent);
            }
    }
};
}  // namespace owner_test
#endif  // TEST_UNIT_QUIC_OWNER_PEER_HPP_
