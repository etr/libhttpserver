/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "test/support/http3_network_owner.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <iostream>
#include <memory>
#include <vector>
namespace h3net {
namespace {
void require(bool result, const char* message) {
    if (!result)
        throw std::runtime_error(message);
}
hs::resource_budget budget() {
    hs::budget_limits limits;
    limits.set(hs::resource::quic_reassembly_bytes, 16777216);
    limits.set(hs::resource::header_fields, 4096);
    return hs::resource_budget::root(limits);
}
hd::quic_recovery_config recovery_config() {
    hd::quic_recovery_config c;
    c.max_information = 128;
    c.max_retained_bytes = 262144;
    c.critical_information = 16;
    c.critical_retained_bytes = 65536;
    c.critical_sent_packets = 16;
    return c;
}
hd::quic_pn_space packet_space(hd::quic_packet_kind kind) {
    if (kind == hd::quic_packet_kind::initial)
        return hd::quic_pn_space::initial;
    if (kind == hd::quic_packet_kind::handshake)
        return hd::quic_pn_space::handshake;
    if (kind == hd::quic_packet_kind::one_rtt)
        return hd::quic_pn_space::application;
    throw std::runtime_error("unsupported packet kind");
}
}  // namespace
connection::connection(hd::tls_credentials_selection credentials, hd::quic_admission_facts facts, const hs::route_registry& routes,
                       httpserver::manual_executor& executor, std::uint64_t id)
    : facts_(std::move(facts)),
      root_(budget()),
      pool_(4194304, 1048576, root_),
      recovery_(recovery_config(), pool_.data(), pool_.critical()),
      routes_(routes),
      executor_(executor),
      id_(id) {
    auto envelope = hd::parse_quic_envelope(facts_.initial->bytes);
    require(envelope.code == hd::quic_codec_code::ok, "admitted Initial envelope");
    client_cid_.assign(envelope.value.source.begin(), envelope.value.source.end());
    server_cid_.assign(facts_.destination.bytes.begin(), facts_.destination.bytes.begin() + facts_.destination.size);
    require(keys_.install_initial(hd::quic_endpoint_role::server, envelope.value.destination) == hd::quic_crypto_code::ok, "Initial keys");
    local_.max_idle_timeout = 15000;
    local_.max_udp_payload_size = 1200;
    local_.initial_max_data = 1048576;
    local_.initial_max_stream_data_bidi_local = 65536;
    local_.initial_max_stream_data_bidi_remote = 65536;
    local_.initial_max_stream_data_uni = 65536;
    local_.initial_max_streams_bidi = 16;
    local_.initial_max_streams_uni = 8;
    local_.initial_source_cid = server_cid_;
    local_.original_destination_cid = std::span(facts_.original_destination.bytes).first(facts_.original_destination.size);
    if (facts_.budget->validated())
        local_.retry_source_cid = server_cid_;
    local_.disable_active_migration = true;
    parameter_bytes_.resize(4096);
    auto encoded = hd::encode_quic_transport_parameters(local_, hd::quic_endpoint_role::server, parameter_bytes_);
    require(encoded.code == hd::quic_codec_code::ok, "local parameters");
    parameter_bytes_.resize(encoded.consumed);
    hd::quic_tls_config config;
    config.local_parameters = parameter_bytes_;
    config.peer_cids.initial_source = client_cid_;
    tls_ = std::make_unique<hd::quic_tls_session>(std::move(credentials), config, pool_.critical(), keys_);
}
connection::~connection() {
    if (pending_) {
        recovery_.abandon_packet(pending_->token);
        facts_.budget->cancel_unsent(*debit_);
    }
    if (engine_)
        engine_->disconnect({httpserver::http::outcome_code::connection_closed, "fixture closed"});
    engine_.reset();
}
void connection::on_datagram(std::shared_ptr<const hd::io_datagram> packet) noexcept {
    try {
        if (!failure_.empty())
            return;
        require(hd::quic_datagram_path(*packet) == std::optional(facts_.budget->path()), "unsupported path change");
        if (!(packet == facts_.initial && !initial_delivered_))
            facts_.budget->receive_datagram(packet->bytes.size());
        initial_delivered_ = true;
        auto bytes = std::span(packet->bytes);
        while (!bytes.empty()) {
            auto envelope = hd::parse_quic_envelope(bytes, server_cid_.size());
            if (envelope.code != hd::quic_codec_code::ok) {
                ++stats_.drops;
                break;
            }
            receive_packet(envelope.value.packet, clock_type::now());
            bytes = bytes.subspan(envelope.consumed);
        }
    } catch (const std::exception& e) {
        failure_ = e.what();
    }
}
void connection::receive_packet(std::span<const std::byte> packet, clock_type::time_point now) {
    auto envelope = hd::parse_quic_envelope(packet, server_cid_.size());
    auto space = packet_space(envelope.value.kind);
    auto index = static_cast<unsigned>(space);
    std::array<std::byte, 65536> plain{}, scratch{};
    auto opened = keys_.open_packet(static_cast<hd::quic_key_level>(index), packet, server_cid_.size(), largest_[index], connected_, plain, scratch);
    if (opened.code != hd::quic_crypto_code::ok) {
        ++stats_.drops;
        return;
    }
    const auto number = opened.header.packet_number;
    auto receipt = recovery_.inspect_received(space, number);
    require(receipt != hd::quic_receipt::invalid, "packet receive capacity");
    auto payload = std::span(plain).first(opened.payload_size);
    hd::quic_frame_cursor cursor;
    std::vector<hd::quic_frame> frames;
    bool eliciting = false;
    while (cursor.offset < payload.size()) {
        auto frame = hd::next_quic_frame(payload, cursor, {opened.header.kind, hd::quic_endpoint_role::client});
        require(frame.code == hd::quic_codec_code::ok, "QUIC frame encoding");
        eliciting |= !std::holds_alternative<hd::quic_padding_frame>(frame.value) && !std::holds_alternative<hd::quic_ack_frame>(frame.value) &&
                     !std::holds_alternative<hd::quic_close_frame>(frame.value);
        frames.push_back(frame.value);
    }
    if (receipt != hd::quic_receipt::fresh) {
        require(static_cast<bool>(recovery_.receive_packet(space, number, eliciting, now)), "duplicate receipt");
        ++stats_.duplicates;
        return;
    }
    for (const auto& frame : frames)
        apply(frame, space, now);
    require(static_cast<bool>(recovery_.receive_packet(space, number, eliciting, now)), "packet receive commit");
    largest_[index] = std::max(number, largest_[index].value_or(0));
    ++stats_.authenticated;
    if (space == hd::quic_pn_space::handshake && !initial_discarded_) {
        keys_.discard_level(hd::quic_key_level::initial);
        recovery_.discard_space(hd::quic_pn_space::initial);
        initial_discarded_ = true;
    }
    if (space == hd::quic_pn_space::handshake)
        facts_.budget->mark_address_validated();
}
void connection::apply(const hd::quic_frame& frame, hd::quic_pn_space space, clock_type::time_point now) {
    if (auto* crypto = std::get_if<hd::quic_crypto_frame>(&frame)) {
        require(static_cast<bool>(tls_->receive(static_cast<hd::quic_crypto_level>(space), crypto->offset, crypto->data)), "TLS CRYPTO receive");
        drive_tls();
    } else if (auto* ack = std::get_if<hd::quic_ack_frame>(&frame)) {
        require(static_cast<bool>(recovery_.receive_ack(space, *ack, now)), "QUIC ACK");
        drain_completions();
    } else if (auto* data = std::get_if<hd::quic_stream_frame>(&frame)) {
        require(connected_, "STREAM before handshake");
        auto& s = stream(data->stream);
        require(static_cast<bool>(flow_->receive(s, *data)), "STREAM flow");
        current_stream_ = data->stream;
        // Admission / body pulls may make an already buffered prefix readable
        // without another datagram. Finish each receive turn before resuming
        // handlers, then revisit the bounded prefix rather than parking it.
        for (unsigned pass = 0; pass < 16; ++pass) {
            engine_->begin_turn();
            engine_->pump_receive(data->stream);
            for (unsigned turn = 0; turn < 256 && executor_.run_one(); ++turn) {
            }
            require(!executor_.pending(), "handler turn capacity");
        }
    } else if (auto* reset = std::get_if<hd::quic_reset_stream_frame>(&frame)) {
        auto& s = stream(reset->stream);
        require(static_cast<bool>(flow_->receive(s, *reset)), "RESET final size");
        if (auto terminal = s.take_terminal())
            engine_->terminal(reset->stream, *terminal);
    } else if (auto* stop = std::get_if<hd::quic_stop_sending_frame>(&frame)) {
        stream(stop->stream);
        engine_->stop_sending(stop->stream, stop->error);
    } else if (auto* flow = std::get_if<hd::quic_flow_frame>(&frame)) {
        require(flow_ && static_cast<bool>(flow_->apply(*flow)), "flow frame");
    } else if (auto* close = std::get_if<hd::quic_close_frame>(&frame)) {
        std::cerr << "QUIC close connection=" << id_ << " application=" << close->application << " code=" << close->error << "\n";
        if (engine_)
            engine_->disconnect({httpserver::http::outcome_code::connection_closed, "fixture closed"});
        failure_ = "peer closed";
    } else if (std::holds_alternative<hd::quic_padding_frame>(frame) || std::holds_alternative<hd::quic_ping_frame>(frame) ||
               std::holds_alternative<hd::quic_new_connection_id_frame>(frame)) {
        // No migration is advertised; the bounded fixture retains its original peer CID.
    } else {
        throw std::runtime_error("unsupported authenticated control");
    }
}
void connection::drive_tls() {
    auto result = connected_ ? tls_->process_post_handshake() : tls_->handshake();
    require(result.state != hd::tls_session::progress::failed && result.state != hd::tls_session::progress::eof, "TLS handshake failed");
    if (!flow_ && !tls_->peer_transport_parameter_bytes().empty()) {
        auto peer = hd::decode_quic_transport_parameters(tls_->peer_transport_parameter_bytes(), hd::quic_endpoint_role::client);
        require(peer.code == hd::quic_codec_code::ok, "peer parameters decode");
        hd::quic_parameter_cid_context c;
        c.initial_source = client_cid_;
        require(hd::validate_quic_transport_parameters(peer.value, hd::quic_endpoint_role::client, c) == hd::quic_codec_code::ok, "peer CID parameters");
        flow_ = std::make_unique<hd::quic_flow_control>(hd::quic_endpoint_role::server, local_, peer.value, 64, pool_.data());
    }
    if (!connected_ && result.state == hd::tls_session::progress::complete) {
        require(tls_->negotiated_protocol() == hd::tls_negotiated_protocol::h3, "ALPN h3");
        connected_ = true;
        require(static_cast<bool>(recovery_.retain_handshake_done()), "retain HANDSHAKE_DONE");
        admit_h3();
    }
}
void connection::admit_h3() {
    require(static_cast<bool>(flow_), "negotiated flow missing");
    hd::http3_request_limits limits;
    limits.max_active_requests = 16;
    engine_ = std::make_unique<hd::http3_request_engine>(pool_.data(), pool_.critical(), *flow_, recovery_, routes_, executor_, limits, id_,
                                                         facts_.initial->peer.peer);
    for (auto role : {hd::http3_role::control, hd::http3_role::qpack_encoder, hd::http3_role::qpack_decoder}) {
        auto opened = flow_->open_local(true);
        require(static_cast<bool>(opened), "local critical open");
        auto s = std::make_unique<hd::quic_stream_state>(opened.id, hd::quic_endpoint_role::server, flow_->ids(), hd::quic_stream_limits{}, pool_.critical());
        require(!engine_->attach_local(role, *s), "local critical attach");
        streams_.emplace(opened.id, std::move(s));
    }
}
hd::quic_stream_state& connection::stream(std::uint64_t id) {
    if (auto found = streams_.find(id); found != streams_.end())
        return *found->second;
    require(flow_ && engine_ && streams_.size() < 64, "stream lifetime capacity");
    require(static_cast<bool>(flow_->observe_peer(id)), "peer stream admission");
    auto s = std::make_unique<hd::quic_stream_state>(id, hd::quic_endpoint_role::server, flow_->ids(), hd::quic_stream_limits{},
                                                     (id & 2) ? pool_.critical() : pool_.data());
    require(!engine_->attach_peer(*s), "H3 stream attach");
    auto& result = *s;
    streams_.emplace(id, std::move(s));
    return result;
}
void connection::drain_completions() {
    while (auto done = recovery_.take_completion()) {
        if (done->kind == hd::quic_information_kind::crypto) {
            auto index = static_cast<unsigned>(done->space);
            // CRYPTO prefixes are retained in copied order; retirement waits for all preceding receipts.
            auto& ranges = crypto_acknowledged_[index];
            auto found = ranges.find(done->id);
            require(found != ranges.end(), "unknown CRYPTO completion");
            found->second.acknowledged = true;
            while (!ranges.empty() && ranges.begin()->second.acknowledged) {
                retired_[index] = ranges.begin()->second.through;
                ranges.erase(ranges.begin());
                require(static_cast<bool>(tls_->retire_output_prefix(static_cast<hd::quic_crypto_level>(index), retired_[index])), "CRYPTO retirement");
            }
        } else if (done->kind == hd::quic_information_kind::flow && done->flow) {
            flow_->credit_acknowledged(*done->flow);
        } else if (engine_ && (done->kind == hd::quic_information_kind::stream || done->kind == hd::quic_information_kind::reset_stream)) {
            engine_->information_completed(*done);
        }
    }
}
hd::quic_recovery_environment connection::environment() const {
    hd::quic_recovery_environment env;
    for (unsigned i = 0; i < 3; ++i)
        env.write_keys[i] = keys_.keys(static_cast<hd::quic_key_level>(i), hd::quic_key_direction::write);
    env.handshake_confirmed = connected_;
    env.peer_validated_endpoint = facts_.budget->validated();
    return env;
}
void connection::retain_output() {
    std::array<std::byte, 4096> bytes{};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned turn = 0; turn < 16; ++turn) {
            auto copy = tls_->copy_output(static_cast<hd::quic_crypto_level>(i), copied_[i], bytes);
            require(static_cast<bool>(copy), "copy CRYPTO");
            if (!copy.bytes)
                break;
            auto retained = recovery_.retain_crypto(static_cast<hd::quic_pn_space>(i), copied_[i], std::span(bytes).first(copy.bytes));
            require(static_cast<bool>(retained), "retain CRYPTO");
            copied_[i] += copy.bytes;
            crypto_acknowledged_[i].emplace(retained.id, crypto_prefix{copied_[i], false});
        }
    }
    if (!engine_)
        return;
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
void connection::tick(clock_type::time_point now) {
    if (!failure_.empty())
        return;
    try {
        require(now < expires_, "connection deadline exceeded");
        recovery_.set_environment(environment(), now);
        auto expired = recovery_.expire(now);
        if (expired.probe_space)
            probes_[static_cast<unsigned>(*expired.probe_space)] += expired.probes;
        drain_completions();
        for (unsigned turn = 0; turn < 256 && executor_.run_one(); ++turn) {
        }
        require(!executor_.pending(), "handler turn capacity");
        retain_output();
    } catch (const std::exception& e) {
        failure_ = e.what();
    }
}
std::optional<transmission> connection::prepare(clock_type::time_point now) {
    if (pending_ || !failure_.empty() || !flow_)
        return {};
    std::array<std::byte, 1200> plain{}, scratch{};
    for (unsigned i = 0; i < 3; ++i) {
        auto level = static_cast<hd::quic_key_level>(i);
        if (!keys_.keys(level, hd::quic_key_direction::write))
            continue;
        auto space = static_cast<hd::quic_pn_space>(i);
        hd::quic_send_request request;
        request.protection_overhead = i == 2 ? client_cid_.size() + 21 : client_cid_.size() + server_cid_.size() + (i == 0 ? 30 : 29);
        request.probe = probes_[i] != 0;
        auto plan = recovery_.prepare_scheduled_packet(space, plain, now, request, *flow_);
        if (!plan)
            continue;
        hd::quic_packet_write write;
        write.packet_number_width = 4;
        if (i != 2)
            write.length_width = 2;
        write.packet_number = plan.packet_number;
        write.kind = i == 0 ? hd::quic_packet_kind::initial : i == 1 ? hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
        write.destination = client_cid_;
        if (i != 2)
            write.source = server_cid_;
        auto size = std::max(plan.bytes, std::size_t{4});
        if (i == 0)
            size = 1200 - request.protection_overhead;
        write.payload = std::span(plain).first(size);
        transmission output;
        output.bytes.resize(1200);
        output.peer = facts_.initial->peer;
        auto protected_packet = keys_.protect_packet(level, write, output.bytes, scratch);
        if (protected_packet.code != hd::quic_crypto_code::ok)
            throw std::runtime_error("protect packet code=" + std::to_string(static_cast<unsigned>(protected_packet.code)) + " level=" + std::to_string(i) +
                                     " payload=" + std::to_string(size));
        output.bytes.resize(protected_packet.consumed);
        auto permission = recovery_.check_scheduled_emission(plan.token, now, output.bytes.size(), plan.ack_eliciting, plan.ack_eliciting);
        auto debit = permission ? facts_.budget->reserve_send(output.bytes.size()) : std::nullopt;
        if (!debit) {
            recovery_.abandon_packet(plan.token);
            continue;
        }
        pending_ = plan;
        pending_space_ = space;
        debit_ = debit;
        pending_bytes_ = output.bytes.size();
        return output;
    }
    return {};
}
void connection::emitted(bool success, clock_type::time_point now) {
    require(pending_.has_value(), "send without preparation");
    const auto plan = *pending_;
    if (success) {
        require(facts_.budget->complete_send(*debit_), "actual amplification debit");
        require(static_cast<bool>(recovery_.commit_sent(plan.token, now, pending_bytes_, plan.ack_eliciting, plan.ack_eliciting,
                                                        keys_.generation(hd::quic_key_direction::write))),
                "actual send commit");
        if (plan.stream)
            require(static_cast<bool>(streams_.at(plan.stream->stream)->record_stream_sent(plan.stream->offset, plan.stream->data.size(), plan.stream->fin)),
                    "stream actual send");
        if (plan.reset)
            require(static_cast<bool>(streams_.at(plan.reset->stream)->record_reset_sent(*plan.reset)), "reset actual send");
        if (plan.flow)
            flow_->credit_emitted(*plan.flow);
        auto index = static_cast<unsigned>(pending_space_);
        if (probes_[index] && plan.ack_eliciting)
            --probes_[index];
        ++stats_.emitted;
    } else {
        recovery_.abandon_packet(plan.token);
        facts_.budget->cancel_unsent(*debit_);
    }
    pending_.reset();
    debit_.reset();
}
std::optional<clock_type::time_point> connection::deadline() const {
    if (!failure_.empty())
        return {};
    auto deadline = recovery_.next_deadline();
    auto due = recovery_.next_send_deadline();
    if (deadline && (!due || deadline->deadline < *due))
        due = deadline->deadline;
    if (!due || expires_ < *due)
        due = expires_;
    return due;
}
}  // namespace h3net
