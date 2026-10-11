/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/rand.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>
#include <httpserver/detail/quic_network_connection.hpp>
#include <httpserver/detail/quic_datagram_sizing.hpp>
namespace httpserver::detail {
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
quic_recovery_config recovery_config(const quic_network_config& config) {
    quic_recovery_config value;
    value.max_datagram_size = 1200;
    value.max_information = 64;
    value.max_sent_packets = 64;
    value.max_retained_bytes = config.data_bytes / 4 + config.critical_bytes / 8;
    value.critical_information = 16;
    value.critical_retained_bytes = config.critical_bytes / 8;
    value.critical_sent_packets = 16;
    return value;
}
class weak_sink final : public datagram_sink {
 public:
    explicit weak_sink(std::weak_ptr<quic_network_connection> target) : target_(std::move(target)) {}
    void on_datagram(std::shared_ptr<const io_datagram> packet) noexcept override {
        if (auto target = target_.lock()) target->on_datagram(std::move(packet));
    }
 private:
    std::weak_ptr<quic_network_connection> target_;
};
}  // namespace
quic_network_connection::quic_network_connection(tls_credentials_selection credentials, quic_admission_facts facts,
    const server::route_registry& routes, executor& wire, std::uint64_t id, const server::peer_policy& policy,
    server::resource_budget budget, quic_network_config config, request_dispatch_context dispatch, http3_native_context native)
    : config_(config), lifecycle_(config.timeouts.body_idle, {}, clock_type::now()), facts_(std::move(facts)), root_(std::move(budget)),
      pool_(config.data_bytes, config.critical_bytes, root_), recovery_(recovery_config(config), pool_.data(), pool_.critical()),
      routes_(routes), executor_(wire), dispatch_(std::move(dispatch)), native_(std::move(native)), id_(id),
      handshake_deadline_(clock_type::now() + config.timeouts.handshake) {
    const auto envelope = parse_quic_envelope(facts_.initial->bytes);
    require(envelope.code == quic_codec_code::ok, "Invalid admitted Initial");
    client_cid_.assign(envelope.value.source.begin(), envelope.value.source.end());
    server_cid_.assign(facts_.destination.bytes.begin(), facts_.destination.bytes.begin() + facts_.destination.size);
    require(keys_.install_initial(quic_endpoint_role::server, envelope.value.destination) == quic_crypto_code::ok, "Initial key installation failed");
    local_.max_idle_timeout = config.timeouts.body_idle.count();
    local_.max_udp_payload_size = 1200;
    local_.initial_max_data = config.data_bytes / 2;
    local_.initial_max_stream_data_bidi_local = local_.initial_max_stream_data_bidi_remote = local_.initial_max_stream_data_uni = std::min<std::size_t>(65536, config.data_bytes / 8);
    local_.initial_max_streams_bidi = config.max_requests;
    local_.initial_max_streams_uni = 8;
    local_.initial_source_cid = server_cid_;
    local_.original_destination_cid = std::span(facts_.original_destination.bytes).first(facts_.original_destination.size);
    if (facts_.budget->validated()) local_.retry_source_cid = server_cid_;
    local_.active_connection_id_limit = quic_connection_identity::max_active_cids;
    quic_cid peer;
    peer.size = client_cid_.size();
    std::copy(client_cid_.begin(), client_cid_.end(), peer.bytes.begin());
    identity_ = std::make_unique<quic_connection_identity>(facts_.destination, peer, facts_.budget, recovery_, policy,
        [](std::span<std::byte> bytes) { return RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), bytes.size()) == 1; }, pool_.critical());
    parameter_bytes_.resize(4096);
    auto encoded = encode_quic_transport_parameters(local_, quic_endpoint_role::server, parameter_bytes_);
    require(encoded.code == quic_codec_code::ok, "Transport parameter encoding failed");
    parameter_bytes_.resize(encoded.consumed);
    quic_tls_config tls;
    tls.local_parameters = parameter_bytes_;
    tls.peer_cids.initial_source = client_cid_;
    tls.connection_id = id;
    tls.listener_port = config.port;
    tls.diagnostics = dispatch_.diagnostics;
    tls.output_capacity = std::min<std::size_t>(16384, config.critical_bytes / 16);
    tls_ = std::make_unique<quic_tls_session>(std::move(credentials), tls, pool_.critical(), keys_);
}
quic_network_connection::~quic_network_connection() {
    if (pending()) emitted(quic_identity_emission::ambiguous, clock_type::now());
}
std::shared_ptr<datagram_sink> quic_network_connection::route_sink() {
    return std::make_shared<weak_sink>(weak_from_this());
}
void quic_network_connection::adopt_routes(quic_datagram_dispatch& routes, io_connection_owner::datagram_port owner,
    quic_datagram_dispatch::cid_registration initial) {
    require(static_cast<bool>(identity_->attach_routes(routes, owner, std::make_shared<weak_sink>(weak_from_this()), std::move(initial))), "CID registration failed");
}
void quic_network_connection::queue_datagram(std::shared_ptr<const io_datagram> packet) {
    server::reservation storage;
    // Capacity pressure from unauthenticated ciphertext only discards input.
    if (received_.size() == 16 || received_bytes_ + packet->bytes.size() > 32768 ||
        !pool_.data().budget.reserve(server::resource::quic_reassembly_bytes, packet->bytes.size() + sizeof(queued_packet), storage).ok()) {
        return;
    }
    received_bytes_ += packet->bytes.size();
    received_.push_back({std::move(packet), std::move(storage)});
}
void quic_network_connection::inspect_reset(const io_datagram& packet, clock_type::time_point now) {
    if (identity_->matches_reset(packet.bytes, packet.peer)) {
        lifecycle_.stateless_reset(now, recovery_.current_pto()); terminal_changed(now);
    }
}
void quic_network_connection::receive_datagram(const io_datagram& packet, const quic_path_identity& path, bool& accounted, clock_type::time_point now) {
    auto bytes = std::span(packet.bytes);
    while (!bytes.empty()) {
        const auto envelope = parse_quic_envelope(bytes, server_cid_.size());
        if (envelope.code != quic_codec_code::ok) {
            if (bytes.size() == packet.bytes.size()) inspect_reset(packet, now);
            break;
        }
        const bool authenticated = receive_packet(envelope.value.packet, path, packet.bytes.size(), accounted, now);
        if (!authenticated && bytes.size() == packet.bytes.size()) inspect_reset(packet, now);
        if (!active()) {
            lifecycle_.closing_packet(now); break;
        }
        bytes = bytes.subspan(envelope.consumed);
    }
}
bool quic_network_connection::credit_datagram(const io_datagram& packet, const quic_path_identity& path) {
    if (path != identity_->application_path()) return false;
    close_blocked_ = false;
    if (!(&packet == facts_.initial.get() && !initial_delivered_)) facts_.budget->receive_datagram(packet.bytes.size());
    return true;
}
void quic_network_connection::signal_progress() {
    if (native_.progress && *native_.progress) (*native_.progress)();
}
void quic_network_connection::on_datagram(std::shared_ptr<const io_datagram> packet) noexcept {
    const auto now = clock_type::now();
    try {
        if (pending()) {
            queue_datagram(std::move(packet));
            return;
        }
        if (receiving_ || lifecycle_.state() == quic_lifecycle_state::closed || lifecycle_.state() == quic_lifecycle_state::draining) return;
        const auto path = quic_datagram_path(*packet);
        if (!path) return;
        bool accounted = credit_datagram(*packet, *path);
        initial_delivered_ = true;
        receiving_ = true;
        receive_datagram(*packet, *path, accounted, now);
        receiving_ = false;
        retire_terminal(now);
        signal_progress();
    } catch (const std::exception&) {
        receiving_ = false;
        request_close({false, 0x01, 0, {}}, now);
    }
}
void quic_network_connection::drain(clock_type::time_point deadline) { drain_requested_ = deadline; }
void quic_network_connection::check_drain(clock_type::time_point now) {
    if (!active() || !engine_) return;
    if (drain_requested_) {
        static_cast<void>(engine_->begin_drain(*drain_requested_, drain_ticket_, now));
        drain_requested_.reset();
    }
    engine_->check_drain(now);
    if (auto close = engine_->take_connection_close()) request_close(*close, now);
}
void quic_network_connection::request_close(const quic_close_frame& frame, clock_type::time_point now) {
    if (lifecycle_.local_close(frame, now, recovery_.current_pto())) terminal_changed(now);
}
void quic_network_connection::terminal_changed(clock_type::time_point now) {
    if (!terminal_stopped_) {
        terminal_stopped_ = true;
        if (engine_) engine_->disconnect({http::outcome_code::connection_closed, "QUIC connection closed"});
        identity_->stop();
        tls_.reset(); probes_.fill(0);
    }
    if (lifecycle_.state() != quic_lifecycle_state::closing) keys_.clear();
    retire_terminal(now);
}
bool quic_network_connection::retired() const {
    return lifecycle_.state() == quic_lifecycle_state::closed && !pending() && (!engine_ || engine_->native_retired());
}
bool quic_network_connection::retire_application() {
    if (lifecycle_.state() == quic_lifecycle_state::closing) {
        try {
            cache_close();
            } catch (...) { cached_close_.reset();
        }
    }
    for (unsigned i = 0; i < 3; ++i) recovery_.discard_space(static_cast<quic_pn_space>(i));
    for (auto& ranges : crypto_acknowledged_) ranges.clear();
    if (engine_ && !engine_->native_retired()) return false;
    engine_.reset(); streams_.clear(); flow_.reset();
    return true;
}
void quic_network_connection::retire_terminal(clock_type::time_point) {
    if (active() || receiving_ || pending()) return;
    if (!terminal_retired_) {
        if (!retire_application()) return;
        terminal_retired_ = true;
    }
    if (lifecycle_.state() != quic_lifecycle_state::closing) {
        keys_.clear(); cached_close_.reset();
    }
    if (lifecycle_.state() == quic_lifecycle_state::closed) identity_->detach_routes();
}
bool quic_network_connection::close_key_available(unsigned i) const {
    if (connected_) return i == 2 && keys_.keys(static_cast<quic_key_level>(i), quic_key_direction::write);
    return i != 2 && keys_.keys(static_cast<quic_key_level>(i), quic_key_direction::write);
}
std::optional<quic_datagram_packet> quic_network_connection::close_packet(unsigned i, std::span<std::byte> plain, const quic_cid& destination) {
    if (!close_key_available(i)) return {};
    auto plan = recovery_.reserve_packet(static_cast<quic_pn_space>(i));
    if (!plan) return {};
    recovery_.abandon_packet(plan.token);  // Burn PN; cached close has no reliable information.
    quic_packet_write write;
    write.kind = i == 0 ? quic_packet_kind::initial : i == 1 ? quic_packet_kind::handshake : quic_packet_kind::one_rtt;
    write.packet_number_width = 4;
    write.packet_number = plan.packet_number;
    write.destination = std::span(destination.bytes).first(destination.size);
    if (i != 2) {
        write.source = server_cid_;
        write.length_width = 2;
    }
    auto frame = encode_quic_frame(lifecycle_.close_frame(i == 2), plain, {write.kind, quic_endpoint_role::server});
    if (frame.code != quic_codec_code::ok) return {};
    // Apply the conservative Initial datagram floor even for terminal output.
    return quic_datagram_packet{write, std::max(frame.consumed, std::size_t{4}), i == 0};
}
void quic_network_connection::cache_close() {
    constexpr std::size_t ceiling = 1200;
    std::array<std::array<std::byte, ceiling>, 3> plain{};
    std::array<quic_datagram_packet, 3> packets{};
    std::array<quic_key_level, 3> levels{};
    std::size_t count = 0;
    const auto destination = identity_->application_cid();
    for (unsigned i = 0; i < 3; ++i) {
        auto packet = close_packet(i, plain[count], destination);
        if (!packet) continue;
        packets[count] = *packet; levels[count++] = static_cast<quic_key_level>(i);
    }
    if (!count) return;
    auto layout = size_quic_datagram(std::span(packets).first(count), quic_endpoint_role::server, recovery_.path_datagram_size());
    if (layout.code != quic_codec_code::ok) return;
    quic_transmission output;
    output.peer = identity_->application_path().peer;
    output.bytes.resize(layout.total);
    std::array<std::byte, ceiling> scratch{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < count; ++i) {
        packets[i].header.payload = std::span(plain[i]).first(layout.payload[i]);
        auto result = keys_.protect_packet(levels[i], packets[i].header, std::span(output.bytes).subspan(at, layout.wire[i]), scratch);
        if (result.code != quic_crypto_code::ok || result.consumed != layout.wire[i]) return;
        at += result.consumed;
    }
    cached_close_ = std::move(output);
}
std::optional<quic_transmission> quic_network_connection::prepare_close(clock_type::time_point now) {
    retire_terminal(now);
    if (!cached_close_ || !lifecycle_.response_due(now)) return {};
    auto budget = identity_->application_budget();
    // Copy before reserving so allocation failure cannot leak amplification debit.
    quic_transmission output = *cached_close_;
    auto debit = budget->reserve_send(output.bytes.size());
    if (!debit) {
        close_blocked_ = true;
        return {};
    }
    close_blocked_ = false;
    pending_budget_ = std::move(budget);
    debit_ = debit;
    close_pending_ = true;
    return output;
}
}  // namespace httpserver::detail
