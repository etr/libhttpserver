/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <memory>
#include <array>
#include <stdexcept>
#include <utility>
#include <httpserver/detail/quic_network_connection.hpp>
#include <httpserver/detail/protocol_limits.hpp>
#include <httpserver/detail/quic_datagram_sizing.hpp>
namespace httpserver::detail {
namespace {
constexpr std::size_t datagram_ceiling = 1200;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
quic_pn_space packet_space(quic_packet_kind kind) {
    if (kind == quic_packet_kind::initial) return quic_pn_space::initial;
    if (kind == quic_packet_kind::handshake) return quic_pn_space::handshake;
    if (kind == quic_packet_kind::one_rtt) return quic_pn_space::application;
    throw std::runtime_error("Invalid packet number space");
}
bool identity_control(const quic_frame& frame) {
    return std::holds_alternative<quic_padding_frame>(frame) || std::holds_alternative<quic_ping_frame>(frame) ||
        std::holds_alternative<quic_new_connection_id_frame>(frame) || std::holds_alternative<quic_retire_connection_id_frame>(frame) ||
        std::holds_alternative<quic_path_frame>(frame);
}
bool probing_control(const quic_frame& frame) {
    return std::holds_alternative<quic_padding_frame>(frame) || std::holds_alternative<quic_path_frame>(frame) ||
        std::holds_alternative<quic_new_connection_id_frame>(frame);
}
struct decoded_frames {
    std::array<quic_frame, 128> frames;
    std::size_t count = 0;
    bool eliciting = false;
};
decoded_frames decode_authenticated_frames(std::span<const std::byte> payload, quic_packet_kind kind) {
    decoded_frames result;
    quic_frame_cursor cursor;
    while (cursor.offset < payload.size()) {
        auto frame = next_quic_frame(payload, cursor, {kind, quic_endpoint_role::client});
        require(frame.code == quic_codec_code::ok, "QUIC frame encoding");
        result.eliciting |= !std::holds_alternative<quic_padding_frame>(frame.value) &&
            !std::holds_alternative<quic_ack_frame>(frame.value) && !std::holds_alternative<quic_close_frame>(frame.value);
        require(result.count < result.frames.size(), "Authenticated frame turn capacity exhausted");
        result.frames[result.count++] = frame.value;
    }
    return result;
}
}  // namespace
bool quic_network_connection::receive_packet(std::span<const std::byte> packet, const quic_path_identity& path, std::size_t credit, bool& accounted, clock_type::time_point now) {
    auto envelope = parse_quic_envelope(packet, server_cid_.size());
    // Disabled 0-RTT and server-only packet types cannot select a receive space.
    if (envelope.code != quic_codec_code::ok ||
        (envelope.value.kind != quic_packet_kind::initial && envelope.value.kind != quic_packet_kind::handshake &&
         envelope.value.kind != quic_packet_kind::one_rtt)) return false;
    auto space = packet_space(envelope.value.kind);
    auto index = static_cast<unsigned>(space);
    std::array<std::byte, 65536> plain{}, scratch{};
    auto opened = keys_.open_packet(static_cast<quic_key_level>(index), packet, server_cid_.size(), largest_[index], connected_, plain, scratch);
    if (opened.code != quic_crypto_code::ok) return false;
    if (active() && space == quic_pn_space::application && keys_.generation(quic_key_direction::read) > keys_.generation(quic_key_direction::write))
        require(keys_.respond_to_peer_update(connected_) == quic_crypto_code::ok, "peer key update response");
    auto decoded = decode_authenticated_frames(std::span(plain).first(opened.payload_size), opened.header.kind);
    quic_cid destination;
    destination.size = envelope.value.destination.size();
    std::copy(envelope.value.destination.begin(), envelope.value.destination.end(), destination.bytes.begin());
    return process_frames(std::span(decoded.frames).first(decoded.count), space, opened.header.packet_number,
        decoded.eliciting, path, destination, credit, accounted, now);
}
bool quic_network_connection::consume_close(std::span<const quic_frame> frames, clock_type::time_point now) {
    // A valid close wins before any CRYPTO, identity or application effects.
    for (const auto& frame : frames) if (const auto* close = std::get_if<quic_close_frame>(&frame)) {
        // Authentication identifies the connection even while a candidate
        // path is unvalidated. Terminal closure does not activate that path.
        lifecycle_.peer_close(*close, now, recovery_.current_pto()); terminal_changed(now);
        return true;
    }
    return false;
}
bool quic_network_connection::accept_identity(std::span<const quic_frame> frames, const quic_path_identity& path,
    const quic_cid& destination, std::uint64_t number, std::size_t credit, bool& accounted, clock_type::time_point now) {
    auto result = identity_->receive({path, destination, number, true, true, accounted ? 0 : credit}, frames, now);
    if (result.code == quic_identity_code::protocol || result.code == quic_identity_code::capacity || result.code == quic_identity_code::no_memory)
        throw std::runtime_error("QUIC identity control error");
    if (!result) return false;
    accounted = true;
    if (result.activated && engine_) engine_->set_peer_for_future_requests(identity_->application_path().peer.peer);
    const bool only_probing = std::all_of(frames.begin(), frames.end(), probing_control);
    return result.allow_payload || only_probing;
}
void quic_network_connection::apply_frames(std::span<const quic_frame> frames, quic_pn_space space, clock_type::time_point now) {
    for (const auto& frame : frames) {
        apply(frame, space, now);
        if (!active()) return;
    }
}
bool quic_network_connection::process_frames(std::span<const quic_frame> frames, quic_pn_space space, std::uint64_t number, bool eliciting,
    const quic_path_identity& path, const quic_cid& destination, std::size_t credit, bool& accounted, clock_type::time_point now) {
    auto receipt = active() ? recovery_.inspect_received(space, number) : quic_receipt::fresh;
    require(receipt != quic_receipt::invalid, "packet receive capacity");
    if (consume_close(frames, now) || !active()) return true;
    if (receipt != quic_receipt::fresh) {
        if (path == identity_->application_path()) {
            require(static_cast<bool>(recovery_.receive_packet(space, number, eliciting, now)), "duplicate receipt");
            lifecycle_.received(now);
        }
        return true;
    }
    if (space == quic_pn_space::application) {
        if (!accept_identity(frames, path, destination, number, credit, accounted, now)) return true;
    } else if (path != identity_->application_path()) {
        return true;
    }
    apply_frames(frames, space, now);
    if (active()) commit_received(space, number, eliciting, now);
    return true;
}
void quic_network_connection::commit_received(quic_pn_space space, std::uint64_t number, bool eliciting, clock_type::time_point now) {
    require(static_cast<bool>(recovery_.receive_packet(space, number, eliciting, now)), "packet receive commit");
    const auto index = static_cast<unsigned>(space);
    largest_[index] = std::max(number, largest_[index].value_or(0));
    lifecycle_.received(now);
    if (space == quic_pn_space::handshake && !initial_discarded_) {
        keys_.discard_level(quic_key_level::initial);
        recovery_.discard_space(quic_pn_space::initial);
        initial_discarded_ = true;
    }
    if (space == quic_pn_space::handshake) facts_.budget->mark_address_validated();
    if (connected_ && keys_.keys(quic_key_level::handshake, quic_key_direction::read)) {
        keys_.discard_level(quic_key_level::handshake);
        recovery_.discard_space(quic_pn_space::handshake);
        recovery_.set_environment(environment(), now);
    }
}
void quic_network_connection::apply_ack(const quic_ack_frame& ack, quic_pn_space space, clock_type::time_point now) {
    require(static_cast<bool>(recovery_.receive_ack(space, ack, now)), "QUIC ACK");
    if (recovery_.pto_count() == 0) probes_.fill(0);
    drain_completions();
}
void quic_network_connection::apply_reset(const quic_reset_stream_frame& reset) {
    auto& s = stream(reset.stream);
    require(static_cast<bool>(flow_->receive(s, reset)), "RESET final size");
    if (auto terminal = s.take_terminal()) engine_->terminal(reset.stream, *terminal);
}
void quic_network_connection::apply(const quic_frame& frame, quic_pn_space space, clock_type::time_point now) {
    if (auto* crypto = std::get_if<quic_crypto_frame>(&frame)) {
        require(static_cast<bool>(tls_->receive(static_cast<quic_crypto_level>(space), crypto->offset, crypto->data)), "TLS CRYPTO receive");
        drive_tls();
    } else if (auto* ack = std::get_if<quic_ack_frame>(&frame)) {
        apply_ack(*ack, space, now);
    } else if (auto* data = std::get_if<quic_stream_frame>(&frame)) {
        require(connected_, "STREAM before handshake");
        auto& s = stream(data->stream);
        require(static_cast<bool>(flow_->receive(s, *data)), "STREAM flow");
        current_stream_ = data->stream;
        // Admission / body pulls may make an already buffered prefix readable
        // without another datagram. Finish each receive turn before resuming
        // handlers, then revisit the bounded prefix rather than parking it.
        engine_->begin_turn();
        engine_->pump_receive(data->stream);
    } else if (auto* reset = std::get_if<quic_reset_stream_frame>(&frame)) {
        apply_reset(*reset);
    } else if (auto* stop = std::get_if<quic_stop_sending_frame>(&frame)) {
        stream(stop->stream);
        engine_->stop_sending(stop->stream, stop->error);
    } else if (auto* flow = std::get_if<quic_flow_frame>(&frame)) {
        require(flow_ && static_cast<bool>(flow_->apply(*flow)), "flow frame");
    } else if (identity_control(frame)) {
        // Identity controls were processed before committing any application data.
    } else {
        throw std::runtime_error("unsupported authenticated control");
    }
}
void quic_network_connection::drive_tls() {
    if (!active()) return;
    auto result = connected_ ? tls_->process_post_handshake() : tls_->handshake();
    if (result.state == tls_session::progress::failed || result.state == tls_session::progress::eof) {
        // Peer TLS errors arbitrate ordinary connection termination, so pending
        // sends and suspended request work follow the same cancellation path.
        const auto alert = tls_->failure().alert;
        request_close({false, alert ? std::uint64_t{0x100} + *alert : 0x01, 0, {}}, clock_type::now());
        return;
    }
    if (!flow_ && !tls_->peer_transport_parameter_bytes().empty()) {
        auto peer = decode_quic_transport_parameters(tls_->peer_transport_parameter_bytes(), quic_endpoint_role::client);
        require(peer.code == quic_codec_code::ok, "peer parameters decode");
        quic_parameter_cid_context c;
        c.initial_source = client_cid_;
        require(validate_quic_transport_parameters(peer.value, quic_endpoint_role::client, c) == quic_codec_code::ok, "peer CID parameters");
        require(static_cast<bool>(recovery_.set_peer_max_datagram_size(peer.value.max_udp_payload_size)), "peer UDP ceiling");
        identity_->set_peer_active_limit(peer.value.active_connection_id_limit);
        const auto peer_idle = std::min<std::uint64_t>(peer.value.max_idle_timeout, std::chrono::milliseconds::max().count());
        lifecycle_.negotiate(std::chrono::milliseconds(local_.max_idle_timeout), std::chrono::milliseconds(peer_idle));
        flow_ = std::make_unique<quic_flow_control>(quic_endpoint_role::server, local_, peer.value, config_.max_stream_records, pool_.data());
    }
    if (!connected_ && result.state == tls_session::progress::complete) {
        require(tls_->negotiated_protocol() == tls_negotiated_protocol::h3, "application ALPN");
        connected_ = true;
        require(static_cast<bool>(recovery_.retain_handshake_done()), "retain HANDSHAKE_DONE");
        admit_h3();
    }
}
void quic_network_connection::admit_h3() {
    require(static_cast<bool>(flow_), "negotiated flow missing");
    const auto limits = config_.request_limits ? *config_.request_limits :
        native_http3_limits(root_, config_.max_requests, config_.max_stream_records, config_.timeouts);
    native_.tls = tls_->peer_metadata();
    engine_ = std::make_unique<http3_request_engine>(pool_.data(), pool_.critical(), *flow_, recovery_, routes_, executor_, limits, id_,
                                                         facts_.initial->peer.peer, dispatch_, native_);
    for (auto role : {http3_role::control, http3_role::qpack_encoder, http3_role::qpack_decoder}) {
        auto opened = flow_->open_local(true);
        require(static_cast<bool>(opened), "local critical open");
        auto s = std::make_unique<quic_stream_state>(opened.id, quic_endpoint_role::server, flow_->ids(), quic_stream_limits{}, pool_.critical());
        require(!engine_->attach_local(role, *s), "local critical attach");
        streams_.emplace(opened.id, std::move(s));
    }
}
quic_stream_state& quic_network_connection::stream(std::uint64_t id) {
    if (auto found = streams_.find(id); found != streams_.end())
        return *found->second;
    require(flow_ && engine_ && streams_.size() < config_.max_stream_records, "stream lifetime capacity");
    require(static_cast<bool>(flow_->observe_peer(id)), "peer stream admission");
    auto s = std::make_unique<quic_stream_state>(id, quic_endpoint_role::server, flow_->ids(), quic_stream_limits{},
                                                     (id & 2) ? pool_.critical() : pool_.data());
    if (engine_) require(!engine_->attach_peer(*s), "H3 stream attach");
    auto& result = *s;
    streams_.emplace(id, std::move(s));
    return result;
}
void quic_network_connection::retire_crypto_prefix(const quic_information_completion& done) {
    auto index = static_cast<unsigned>(done.space);
    // CRYPTO prefixes retire in copied order, after all preceding receipts.
    auto& ranges = crypto_acknowledged_[index];
    auto found = ranges.find(done.id);
    require(found != ranges.end(), "unknown CRYPTO completion");
    found->second.acknowledged = true;
    while (!ranges.empty() && ranges.begin()->second.acknowledged) {
        retired_[index] = ranges.begin()->second.through;
        ranges.erase(ranges.begin());
        require(static_cast<bool>(tls_->retire_output_prefix(static_cast<quic_crypto_level>(index), retired_[index])), "CRYPTO retirement");
    }
}
void quic_network_connection::drain_completions() {
    if (!active()) return;
    while (auto done = recovery_.take_completion()) {
        identity_->information_completed(*done);
        if (done->kind == quic_information_kind::crypto) {
            retire_crypto_prefix(*done);
        } else if (done->kind == quic_information_kind::flow && done->flow) {
            flow_->credit_acknowledged(*done->flow);
        } else if (engine_ && (done->kind == quic_information_kind::stream || done->kind == quic_information_kind::reset_stream)) {
            if (done->kind == quic_information_kind::reset_stream) {
                auto found = streams_.find(done->stream);
                if (found != streams_.end()) {
                    require(static_cast<bool>(found->second->acknowledge_reset()), "RESET acknowledgement");
                    static_cast<void>(flow_->retire(*found->second));
                }
            }
            engine_->information_completed(*done);
        }
    }
}
quic_recovery_environment quic_network_connection::environment() const {
    quic_recovery_environment env;
    for (unsigned i = 0; i < 3; ++i) env.write_keys[i] = keys_.keys(static_cast<quic_key_level>(i), quic_key_direction::write);
    env.handshake_confirmed = connected_;
    env.peer_validated_endpoint = identity_->application_budget()->validated();
    return env;
}
void quic_network_connection::retain_crypto_output() {
    if (!active()) return;
    std::array<std::byte, 4096> bytes{};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned turn = 0; turn < 16; ++turn) {
            auto copy = tls_->copy_output(static_cast<quic_crypto_level>(i), copied_[i], bytes);
            require(static_cast<bool>(copy), "copy CRYPTO");
            if (!copy.bytes)
                break;
            auto retained = recovery_.retain_crypto(static_cast<quic_pn_space>(i), copied_[i], std::span(bytes).first(copy.bytes));
            require(static_cast<bool>(retained), "retain CRYPTO");
            copied_[i] += copy.bytes;
            crypto_acknowledged_[i].emplace(retained.id, crypto_prefix{copied_[i], false});
        }
    }
}
void quic_network_connection::retain_output() {
    if (!active()) return;
    retain_crypto_output();
    if (!engine_) return;
    // A semantic writer can expose DATA or FIN after its HEADERS were retained,
    // without producing a socket event. Drain a bounded owner turn before parking.
    for (unsigned turn = 0; turn < 16; ++turn) {
        engine_->begin_turn();
        engine_->pump_output();
    }
    require(!engine_->failure(), "H3 engine failure");
    if (auto credit = flow_->pending_credit())
        require(static_cast<bool>(recovery_.retain_flow(*credit)), "retain consumption credit");
    while (auto action = engine_->take_action()) {
        if (action->stop_sending)
            require(static_cast<bool>(recovery_.retain_stop_sending(*action->stop_sending)), "retain STOP_SENDING");
        if (action->reset)
            require(static_cast<bool>(recovery_.retain_reset(*action->reset)), "retain RESET_STREAM");
    }
}
void quic_network_connection::receive_queued_datagrams() {
    while (!pending() && !received_.empty()) {
        auto received = std::move(received_.front()); received_.pop_front();
        received_bytes_ -= received.packet->bytes.size();
        on_datagram(std::move(received.packet));
    }
}
void quic_network_connection::tick(clock_type::time_point now) {
    if (failed_) return;
    try {
        receive_queued_datagrams();
        check_drain(now);
        if (engine_) engine_->expire(now);
        if (lifecycle_.expire(now, recovery_.current_pto())) terminal_changed(now);
        if (!active()) {
            retire_terminal(now);
            return;
        }
        if (!connected_ && now >= handshake_deadline_) {
            request_close({false, 0x01, 0, {}}, now);
            return;
        }
        identity_->expire(now);
        if (connected_) identity_->issue_cid();
        recovery_.set_environment(environment(), now);
        auto expired = recovery_.expire(now);
        if (expired.probe_space) probes_[static_cast<unsigned>(*expired.probe_space)] = expired.probes;
        drain_completions();
        retain_output();
        check_drain(now);
    } catch (const std::exception& e) {
        request_close({false, 0x01, 0, {}}, now);
    }
}
std::optional<quic_transmission> quic_network_connection::prepare(clock_type::time_point now) {
    check_drain(now);
    if (pending() || failed_) return {};
    if (!active()) return prepare_close(now);
    if (!flow_) return {};
    std::array<std::byte, datagram_ceiling> plain{}, scratch{};
    if (connected_) {
        auto control = identity_->prepare_control(now, 21);
        if (control) {
            const auto encoded = encode_quic_frame(quic_path_frame{control->response, control->data}, plain);
            require(encoded.code == quic_codec_code::ok, "path control encode");
            quic_packet_write write;
            write.kind = quic_packet_kind::one_rtt;
            write.packet_number_width = 4;
            write.packet_number = control->packet.packet_number;
            write.destination = std::span(control->destination.bytes).first(control->destination.size);
            write.payload = std::span(plain).first(control->packet.bytes);
            quic_transmission output;
            output.peer = control->path.peer;
            output.bytes.resize(control->wire_bytes);
            auto protected_packet = keys_.protect_packet(quic_key_level::application, write, output.bytes, scratch);
            require(protected_packet.code == quic_crypto_code::ok && protected_packet.consumed == control->wire_bytes, "path packet protect");
            pending_control_ = *control;
            return output;
        }
    }
    return prepare_datagram(now, identity_->application_cid());
}
quic_packet_write quic_network_connection::packet_header(unsigned i, const quic_cid& destination) const {
    quic_packet_write write;
    write.kind = i == 0 ? quic_packet_kind::initial : i == 1 ? quic_packet_kind::handshake : quic_packet_kind::one_rtt;
    write.packet_number_width = 4;
    write.destination = std::span(destination.bytes).first(destination.size);
    if (i != 2) {
        write.length_width = 2;
        write.source = server_cid_;
    }
    return write;
}
void quic_network_connection::prepare_packets(datagram_preparation& batch, clock_type::time_point now, const quic_cid& destination) {
    const auto ceiling = recovery_.path_datagram_size();
    std::size_t used = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!keys_.keys(static_cast<quic_key_level>(i), quic_key_direction::write)) continue;
        auto write = packet_header(i, destination);
        const std::array<quic_datagram_packet, 1> measure{{{write, 4, false}}};
        auto minimum = size_quic_datagram(measure, quic_endpoint_role::server, ceiling);
        require(minimum.code == quic_codec_code::ok, "packet size metadata");
        const auto overhead = minimum.wire[0] - 4;
        if (used + overhead + 4 > ceiling) continue;
        auto space = static_cast<quic_pn_space>(i);
        quic_send_request request{ceiling - used, overhead, probes_[i] != 0};
        auto plan = recovery_.prepare_datagram_packet(space, batch.plain[batch.count], now, request, *flow_);
        if (!plan) continue;
        write.packet_number = plan.packet_number;
        batch.packets[batch.count] = {write, plan.bytes, plan.ack_eliciting};
        batch.plans[batch.count] = plan;
        batch.spaces[batch.count++] = space;
        used += overhead + plan.bytes;
    }
}
quic_transmission quic_network_connection::protect_datagram(datagram_preparation& batch) {
    const auto count = batch.count;
    auto layout = size_quic_datagram(std::span(batch.packets).first(count), quic_endpoint_role::server, recovery_.path_datagram_size());
    require(layout.code == quic_codec_code::ok, "protected datagram layout");
    quic_transmission output;
    output.peer = identity_->application_path().peer;
    output.bytes.resize(layout.total);
    std::array<std::byte, datagram_ceiling> scratch{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto& header = batch.packets[i].header;
        header.payload = std::span(batch.plain[i]).first(layout.payload[i]);
        auto protected_packet = keys_.protect_packet(static_cast<quic_key_level>(batch.spaces[i]), header,
            std::span(output.bytes).subspan(at, layout.wire[i]), scratch);
        require(protected_packet.code == quic_crypto_code::ok && protected_packet.consumed == layout.wire[i], "protect coalesced packet");
        const auto& plan = batch.plans[i];
        pending_packets_[i] = {plan.token, protected_packet.consumed, plan.ack_eliciting, plan.ack_eliciting || layout.padding[i],
            keys_.generation(quic_key_direction::write)};
        at += protected_packet.consumed;
    }
    return output;
}
std::optional<quic_transmission> quic_network_connection::prepare_datagram(clock_type::time_point now, const quic_cid& destination) {
    require(static_cast<bool>(recovery_.begin_datagram()), "begin UDP transaction");
    try {
        datagram_preparation batch;
        prepare_packets(batch, now, destination);
        if (!batch.count) {
            recovery_.abandon_datagram();
            return prepare_probe(now, destination);
        }
        auto output = protect_datagram(batch);
        auto permission = recovery_.check_datagram_emission(std::span(pending_packets_).first(batch.count), now);
        auto budget = identity_->application_budget();
        auto debit = permission ? budget->reserve_send(output.bytes.size()) : std::nullopt;
        if (!debit) {
            recovery_.abandon_datagram();
            return {};
        }
        pending_ = batch.plans[0];
        pending_plans_ = batch.plans; pending_spaces_ = batch.spaces;
        pending_count_ = batch.count; pending_batch_ = true;
        debit_ = debit; pending_budget_ = std::move(budget); pending_bytes_ = output.bytes.size();
        return output;
    } catch (...) {
        recovery_.abandon_datagram(); throw;
    }
}
std::optional<quic_transmission> quic_network_connection::prepare_probe(clock_type::time_point now, const quic_cid& destination) {
    std::array<std::byte, datagram_ceiling> plain{}, scratch{};
    const auto overhead = destination.size + 21;
    auto plan = recovery_.prepare_pmtu_probe(plain, now, overhead);
    if (!plan) return {};
    quic_packet_write write;
    write.kind = quic_packet_kind::one_rtt;
    write.packet_number_width = 4;
    write.packet_number = plan.packet_number;
    write.destination = std::span(destination.bytes).first(destination.size);
    write.payload = std::span(plain).first(plan.bytes);
    quic_transmission output;
    output.peer = identity_->application_path().peer;
    output.bytes.resize(plan.bytes + overhead);
    auto protected_packet = keys_.protect_packet(quic_key_level::application, write, output.bytes, scratch);
    auto permission = protected_packet.code == quic_crypto_code::ok ?
        recovery_.check_scheduled_emission(plan.token, now, protected_packet.consumed, true, true) : quic_send_permission{{quic_recovery_code::invalid}};
    auto budget = identity_->application_budget();
    auto debit = permission ? budget->reserve_send(output.bytes.size()) : std::nullopt;
    if (!debit) {
        recovery_.abandon_packet(plan.token);
        return {};
    }
    pending_ = plan;
    pending_count_ = 1;
    pending_batch_ = false;
    pending_plans_[0] = plan;
    pending_spaces_[0] = quic_pn_space::application;
    debit_ = debit;
    pending_budget_ = std::move(budget);
    pending_bytes_ = output.bytes.size();
    return output;
}
}  // namespace httpserver::detail
