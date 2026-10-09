/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "test/support/http3_network_owner.hpp"
#include <openssl/rand.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <iostream>
#include <memory>
#include <vector>
#include <httpserver/detail/quic_datagram_sizing.hpp>
namespace h3net {
namespace {
constexpr std::size_t datagram_ceiling = 1472;
void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}
hs::resource_budget budget() {
    hs::budget_limits limits;
    limits.set(hs::resource::quic_reassembly_bytes, 16777216);
    limits.set(hs::resource::header_fields, 4096);
    return hs::resource_budget::root(limits);
}
hd::quic_recovery_config recovery_config() {
    hd::quic_recovery_config c;
    c.max_datagram_size = datagram_ceiling;
    c.max_information = 128;
    c.max_retained_bytes = 262144;
    c.critical_information = 16;
    c.critical_retained_bytes = 65536;
    c.critical_sent_packets = 16;
    return c;
}
hd::quic_pn_space packet_space(hd::quic_packet_kind kind) {
    if (kind == hd::quic_packet_kind::initial) return hd::quic_pn_space::initial;
    if (kind == hd::quic_packet_kind::handshake) return hd::quic_pn_space::handshake;
    if (kind == hd::quic_packet_kind::one_rtt) return hd::quic_pn_space::application;
    throw std::runtime_error("unsupported packet kind");
}
}  // namespace
connection::connection(hd::tls_credentials_selection credentials, hd::quic_admission_facts facts, const hs::route_registry& routes, httpserver::manual_executor& executor, std::uint64_t id,
                       const hs::peer_policy& policy, const std::string& file_root)
    : facts_(std::move(facts)), root_(budget()), pool_(4194304, 1048576, root_), recovery_(recovery_config(), pool_.data(), pool_.critical()), routes_(routes), executor_(executor), id_(id) {
    if (!file_root.empty()) {
        files_ = std::make_unique<hqnet::files>(file_root);
        expires_ = clock_type::now() + std::chrono::seconds(120);
    }
    auto envelope = hd::parse_quic_envelope(facts_.initial->bytes);
    require(envelope.code == hd::quic_codec_code::ok, "admitted Initial envelope");
    client_cid_.assign(envelope.value.source.begin(), envelope.value.source.end());
    server_cid_.assign(facts_.destination.bytes.begin(), facts_.destination.bytes.begin() + facts_.destination.size);
    require(keys_.install_initial(hd::quic_endpoint_role::server, envelope.value.destination) == hd::quic_crypto_code::ok, "Initial keys");
    local_.max_idle_timeout = files_ ? 120000 : 15000;
    local_.max_udp_payload_size = datagram_ceiling;
    local_.initial_max_data = 1048576;
    local_.initial_max_stream_data_bidi_local = 65536;
    local_.initial_max_stream_data_bidi_remote = 65536;
    local_.initial_max_stream_data_uni = 65536;
    local_.initial_max_streams_bidi = 16;
    local_.initial_max_streams_uni = 8;
    local_.initial_source_cid = server_cid_;
    local_.original_destination_cid = std::span(facts_.original_destination.bytes).first(facts_.original_destination.size);
    if (facts_.budget->validated()) local_.retry_source_cid = server_cid_;
    local_.active_connection_id_limit = hd::quic_connection_identity::max_active_cids;
    hd::quic_cid peer_cid;
    peer_cid.size = client_cid_.size();
    std::copy(client_cid_.begin(), client_cid_.end(), peer_cid.bytes.begin());
    identity_ = std::make_unique<hd::quic_connection_identity>(
        facts_.destination, peer_cid, facts_.budget, recovery_, policy, [](std::span<std::byte> bytes) { return RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), bytes.size()) == 1; },
        pool_.critical());
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
        if (pending_batch_) recovery_.abandon_datagram();
        else recovery_.abandon_packet(pending_->token);
        pending_budget_->cancel_unsent(*debit_);
    }
    if (pending_control_) identity_->complete_control(pending_control_->token, hd::quic_identity_emission::unsent, {});
    if (engine_) engine_->disconnect({httpserver::http::outcome_code::connection_closed, "fixture closed"});
    engine_.reset();
}
namespace {
class weak_connection_sink final : public hd::datagram_sink {
 public:
    explicit weak_connection_sink(std::weak_ptr<connection> self) : self_(std::move(self)) {}
    void on_datagram(std::shared_ptr<const hd::io_datagram> packet) noexcept override {
        if (auto self = self_.lock()) self->on_datagram(std::move(packet));
    }

 private:
    std::weak_ptr<connection> self_;
};
}  // namespace
void connection::adopt_routes(hd::quic_datagram_dispatch& routes, hd::io_connection_owner::datagram_port owner, hd::quic_datagram_dispatch::cid_registration initial, std::weak_ptr<connection> self) {
    require(static_cast<bool>(identity_->attach_routes(routes, owner, std::make_shared<weak_connection_sink>(std::move(self)), std::move(initial))), "CID route adoption");
}
void connection::on_datagram(std::shared_ptr<const hd::io_datagram> packet) noexcept {
    try {
        if (!failure_.empty()) return;
        // Submitted UDP sends remain pinned until their actual completion. Do
        // not mutate CID/path selection or ACK dropped payload while pinned.
        if (pending_ || pending_control_) {
            ++stats_.drops;
            return;
        }
        const auto path = hd::quic_datagram_path(*packet);
        if (!path) {
            ++stats_.drops;
            return;
        }
        bool accounted = false;
        if (*path == identity_->application_path()) {
            if (!(packet == facts_.initial && !initial_delivered_)) identity_->application_budget()->receive_datagram(packet->bytes.size());
            accounted = true;
        }
        initial_delivered_ = true;
        auto bytes = std::span(packet->bytes);
        while (!bytes.empty()) {
            auto envelope = hd::parse_quic_envelope(bytes, server_cid_.size());
            if (envelope.code != hd::quic_codec_code::ok) {
                ++stats_.drops;
                break;
            }
            receive_packet(envelope.value.packet, *path, packet->bytes.size(), accounted, clock_type::now());
            bytes = bytes.subspan(envelope.consumed);
        }
    } catch (const std::exception& e) {
        failure_ = e.what();
    }
}
void connection::receive_packet(std::span<const std::byte> packet, const hd::quic_path_identity& path, std::size_t credit, bool& accounted, clock_type::time_point now) {
    auto envelope = hd::parse_quic_envelope(packet, server_cid_.size());
    auto space = packet_space(envelope.value.kind);
    auto index = static_cast<unsigned>(space);
    std::array<std::byte, 65536> plain{}, scratch{};
    auto opened = keys_.open_packet(static_cast<hd::quic_key_level>(index), packet, server_cid_.size(), largest_[index], connected_, plain, scratch);
    if (opened.code != hd::quic_crypto_code::ok) {
        ++stats_.drops;
        return;
    }
    if (space == hd::quic_pn_space::application && keys_.generation(hd::quic_key_direction::read) > keys_.generation(hd::quic_key_direction::write))
        require(keys_.respond_to_peer_update(connected_) == hd::quic_crypto_code::ok, "peer key update response");
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
        eliciting |=
            !std::holds_alternative<hd::quic_padding_frame>(frame.value) && !std::holds_alternative<hd::quic_ack_frame>(frame.value) && !std::holds_alternative<hd::quic_close_frame>(frame.value);
        frames.push_back(frame.value);
    }
    if (receipt != hd::quic_receipt::fresh) {
        if (path != identity_->application_path()) {
            ++stats_.duplicates;
            return;
        }
        require(static_cast<bool>(recovery_.receive_packet(space, number, eliciting, now)), "duplicate receipt");
        ++stats_.duplicates;
        return;
    }
    if (space == hd::quic_pn_space::application) {
        hd::quic_cid destination;
        destination.size = envelope.value.destination.size();
        std::copy(envelope.value.destination.begin(), envelope.value.destination.end(), destination.bytes.begin());
        auto result = identity_->receive({path, destination, number, true, true, accounted ? 0 : credit}, frames, now);
        if (result.code == hd::quic_identity_code::protocol || result.code == hd::quic_identity_code::capacity || result.code == hd::quic_identity_code::no_memory)
            throw std::runtime_error("QUIC identity control error");
        if (!result) {
            ++stats_.drops;
            return;
        }
        accounted = true;
        if (result.activated && engine_) engine_->set_peer_for_future_requests(identity_->application_path().peer.peer);
        const bool only_probing = std::all_of(frames.begin(), frames.end(), [](const auto& frame) {
            return std::holds_alternative<hd::quic_padding_frame>(frame) || std::holds_alternative<hd::quic_path_frame>(frame) || std::holds_alternative<hd::quic_new_connection_id_frame>(frame);
        });
        if (!result.allow_payload && !only_probing) {
            ++stats_.drops;
            return;
        }
    } else if (path != identity_->application_path()) {
        ++stats_.drops;
        return;
    }
    for (const auto& frame : frames) apply(frame, space, now);
    require(static_cast<bool>(recovery_.receive_packet(space, number, eliciting, now)), "packet receive commit");
    largest_[index] = std::max(number, largest_[index].value_or(0));
    ++stats_.authenticated;
    if (space == hd::quic_pn_space::handshake && !initial_discarded_) {
        keys_.discard_level(hd::quic_key_level::initial);
        recovery_.discard_space(hd::quic_pn_space::initial);
        initial_discarded_ = true;
    }
    if (space == hd::quic_pn_space::handshake) facts_.budget->mark_address_validated();
}
void connection::apply(const hd::quic_frame& frame, hd::quic_pn_space space, clock_type::time_point now) {
    if (auto* crypto = std::get_if<hd::quic_crypto_frame>(&frame)) {
        require(static_cast<bool>(tls_->receive(static_cast<hd::quic_crypto_level>(space), crypto->offset, crypto->data)), "TLS CRYPTO receive");
        drive_tls();
    } else if (auto* ack = std::get_if<hd::quic_ack_frame>(&frame)) {
        require(static_cast<bool>(recovery_.receive_ack(space, *ack, now)), "QUIC ACK");
        if (recovery_.pto_count() == 0) probes_.fill(0);
        drain_completions();
    } else if (auto* data = std::get_if<hd::quic_stream_frame>(&frame)) {
        require(connected_, "STREAM before handshake");
        auto& s = stream(data->stream);
        require(static_cast<bool>(flow_->receive(s, *data)), "STREAM flow");
        current_stream_ = data->stream;
        // Admission / body pulls may make an already buffered prefix readable
        // without another datagram. Finish each receive turn before resuming
        // handlers, then revisit the bounded prefix rather than parking it.
        if (files_) {
            files_->receive(s, *flow_);
            return;
        }
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
        if (files_) files_->cancel(reset->stream, recovery_);
        else if (auto terminal = s.take_terminal()) engine_->terminal(reset->stream, *terminal);
    } else if (auto* stop = std::get_if<hd::quic_stop_sending_frame>(&frame)) {
        stream(stop->stream);
        if (files_) {
            files_->cancel(stop->stream, recovery_);
            require(static_cast<bool>(recovery_.retain_reset({stop->stream, stop->error, streams_.at(stop->stream)->highest_sent()})), "hq RESET retention");
        } else {
            engine_->stop_sending(stop->stream, stop->error);
        }
    } else if (auto* flow = std::get_if<hd::quic_flow_frame>(&frame)) {
        require(flow_ && static_cast<bool>(flow_->apply(*flow)), "flow frame");
    } else if (auto* close = std::get_if<hd::quic_close_frame>(&frame)) {
        std::cerr << "QUIC close connection=" << id_ << " application=" << close->application << " code=" << close->error << "\n";
        if (engine_) engine_->disconnect({httpserver::http::outcome_code::connection_closed, "fixture closed"});
        failure_ = "peer closed";
    } else if (std::holds_alternative<hd::quic_padding_frame>(frame) || std::holds_alternative<hd::quic_ping_frame>(frame) || std::holds_alternative<hd::quic_new_connection_id_frame>(frame) ||
               std::holds_alternative<hd::quic_retire_connection_id_frame>(frame) || std::holds_alternative<hd::quic_path_frame>(frame)) {
        // Identity controls were processed before committing any application data.
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
        require(static_cast<bool>(recovery_.set_peer_max_datagram_size(peer.value.max_udp_payload_size)), "peer UDP ceiling");
        identity_->set_peer_active_limit(peer.value.active_connection_id_limit);
        flow_ = std::make_unique<hd::quic_flow_control>(hd::quic_endpoint_role::server, local_, peer.value, 64, pool_.data());
    }
    if (!connected_ && result.state == hd::tls_session::progress::complete) {
        require(tls_->negotiated_protocol() == (files_ ? hd::tls_negotiated_protocol::other : hd::tls_negotiated_protocol::h3), "application ALPN");
        connected_ = true;
        require(static_cast<bool>(recovery_.retain_handshake_done()), "retain HANDSHAKE_DONE");
        if (!files_) admit_h3();
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
    require(flow_ && (engine_ || files_) && streams_.size() < 64 && (!files_ || (id & 3) == 0), "stream lifetime capacity");
    require(static_cast<bool>(flow_->observe_peer(id)), "peer stream admission");
    auto s = std::make_unique<hd::quic_stream_state>(id, hd::quic_endpoint_role::server, flow_->ids(), hd::quic_stream_limits{},
                                                     (id & 2) ? pool_.critical() : pool_.data());
    if (engine_) require(!engine_->attach_peer(*s), "H3 stream attach");
    auto& result = *s;
    streams_.emplace(id, std::move(s));
    return result;
}
void connection::drain_completions() {
    while (auto done = recovery_.take_completion()) {
        identity_->information_completed(*done);
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
        } else if (files_ && done->kind == hd::quic_information_kind::stream) {
            files_->completed(*done);
        } else if (engine_ && (done->kind == hd::quic_information_kind::stream || done->kind == hd::quic_information_kind::reset_stream)) {
            engine_->information_completed(*done);
        }
    }
}
hd::quic_recovery_environment connection::environment() const {
    hd::quic_recovery_environment env;
    for (unsigned i = 0; i < 3; ++i) env.write_keys[i] = keys_.keys(static_cast<hd::quic_key_level>(i), hd::quic_key_direction::write);
    env.handshake_confirmed = connected_;
    env.peer_validated_endpoint = identity_->application_budget()->validated();
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
    if (files_) {
        if (!flow_) return;
        files_->pump(*flow_, recovery_);
        if (auto credit = flow_->pending_credit()) require(static_cast<bool>(recovery_.retain_flow(*credit)), "hq consumption credit");
        return;
    }
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
void connection::tick(clock_type::time_point now) {
    if (!failure_.empty()) return;
    try {
        require(now < expires_, "connection deadline exceeded");
        identity_->expire(now);
        if (connected_) identity_->issue_cid();
        recovery_.set_environment(environment(), now);
        auto expired = recovery_.expire(now);
        if (expired.probe_space) probes_[static_cast<unsigned>(*expired.probe_space)] = expired.probes;
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
    if (pending_ || pending_control_ || !failure_.empty() || !flow_) return {};
    std::array<std::byte, datagram_ceiling> plain{}, scratch{};
    if (connected_) {
        auto control = identity_->prepare_control(now, 21);
        if (control) {
            const auto encoded = hd::encode_quic_frame(hd::quic_path_frame{control->response, control->data}, plain);
            require(encoded.code == hd::quic_codec_code::ok, "path control encode");
            hd::quic_packet_write write;
            write.kind = hd::quic_packet_kind::one_rtt;
            write.packet_number_width = 4;
            write.packet_number = control->packet.packet_number;
            write.destination = std::span(control->destination.bytes).first(control->destination.size);
            write.payload = std::span(plain).first(control->packet.bytes);
            transmission output;
            output.peer = control->path.peer;
            output.bytes.resize(control->wire_bytes);
            auto protected_packet = keys_.protect_packet(hd::quic_key_level::application, write, output.bytes, scratch);
            require(protected_packet.code == hd::quic_crypto_code::ok && protected_packet.consumed == control->wire_bytes, "path packet protect");
            pending_control_ = *control;
            return output;
        }
    }
    return prepare_datagram(now, identity_->application_cid());
}
std::optional<transmission> connection::prepare_datagram(clock_type::time_point now, const hd::quic_cid& destination) {
    require(static_cast<bool>(recovery_.begin_datagram()), "begin UDP transaction");
    try {
        std::array<std::array<std::byte, datagram_ceiling>, 3> plain{};
        std::array<std::byte, datagram_ceiling> scratch{};
        std::array<hd::quic_datagram_packet, 3> packets{};
        std::array<hd::quic_send_plan, 3> plans{};
        std::array<hd::quic_pn_space, 3> spaces{};
        std::size_t count = 0, used = 0;
        const auto ceiling = recovery_.path_datagram_size();
        for (unsigned i = 0; i < 3; ++i) {
            auto level = static_cast<hd::quic_key_level>(i);
            if (!keys_.keys(level, hd::quic_key_direction::write)) continue;
            hd::quic_packet_write write;
            write.kind = i == 0 ? hd::quic_packet_kind::initial : i == 1 ? hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
            write.packet_number_width = 4;
            if (i != 2) write.length_width = 2;
            write.destination = std::span(destination.bytes).first(destination.size);
            if (i != 2) write.source = server_cid_;
            const std::array<hd::quic_datagram_packet, 1> measure{{{write, 4, false}}};
            auto minimum = hd::size_quic_datagram(measure, hd::quic_endpoint_role::server, ceiling);
            require(minimum.code == hd::quic_codec_code::ok, "packet size metadata");
            const auto overhead = minimum.wire[0] - 4;
            if (used + overhead + 4 > ceiling) continue;
            auto space = static_cast<hd::quic_pn_space>(i);
            hd::quic_send_request request{ceiling - used, overhead, probes_[i] != 0};
            auto plan = recovery_.prepare_datagram_packet(space, plain[count], now, request, *flow_);
            if (!plan) continue;
            write.packet_number = plan.packet_number;
            packets[count] = {write, plan.bytes, plan.ack_eliciting};
            plans[count] = plan;
            spaces[count] = space;
            used += overhead + plan.bytes;
            ++count;
        }
        if (!count) {
            recovery_.abandon_datagram();
            return prepare_probe(now, destination);
        }
        auto layout = hd::size_quic_datagram(std::span(packets).first(count), hd::quic_endpoint_role::server, ceiling);
        require(layout.code == hd::quic_codec_code::ok, "protected datagram layout");
        transmission output;
        output.peer = identity_->application_path().peer;
        output.bytes.resize(layout.total);
        std::size_t at = 0;
        for (std::size_t i = 0; i < count; ++i) {
            packets[i].header.payload = std::span(plain[i]).first(layout.payload[i]);
            auto protected_packet = keys_.protect_packet(static_cast<hd::quic_key_level>(spaces[i]), packets[i].header,
                                                        std::span(output.bytes).subspan(at, layout.wire[i]), scratch);
            require(protected_packet.code == hd::quic_crypto_code::ok && protected_packet.consumed == layout.wire[i], "protect coalesced packet");
            pending_packets_[i] = {plans[i].token, protected_packet.consumed, plans[i].ack_eliciting, plans[i].ack_eliciting || layout.padding[i],
                                   keys_.generation(hd::quic_key_direction::write)};
            at += protected_packet.consumed;
        }
        auto permission = recovery_.check_datagram_emission(std::span(pending_packets_).first(count), now);
        auto budget = identity_->application_budget();
        auto debit = permission ? budget->reserve_send(output.bytes.size()) : std::nullopt;
        if (!debit) {
            recovery_.abandon_datagram();
            return {};
        }
        pending_ = plans[0];
        pending_plans_ = plans;
        pending_spaces_ = spaces;
        pending_count_ = count;
        pending_batch_ = true;
        debit_ = debit;
        pending_budget_ = std::move(budget);
        pending_bytes_ = output.bytes.size();
        return output;
    } catch (...) {
        recovery_.abandon_datagram();
        throw;
    }
}
std::optional<transmission> connection::prepare_probe(clock_type::time_point now, const hd::quic_cid& destination) {
    std::array<std::byte, datagram_ceiling> plain{}, scratch{};
    const auto overhead = destination.size + 21;
    auto plan = recovery_.prepare_pmtu_probe(plain, now, overhead);
    if (!plan) return {};
    hd::quic_packet_write write;
    write.kind = hd::quic_packet_kind::one_rtt;
    write.packet_number_width = 4;
    write.packet_number = plan.packet_number;
    write.destination = std::span(destination.bytes).first(destination.size);
    write.payload = std::span(plain).first(plan.bytes);
    transmission output;
    output.peer = identity_->application_path().peer;
    output.bytes.resize(plan.bytes + overhead);
    auto protected_packet = keys_.protect_packet(hd::quic_key_level::application, write, output.bytes, scratch);
    auto permission = protected_packet.code == hd::quic_crypto_code::ok ?
        recovery_.check_scheduled_emission(plan.token, now, protected_packet.consumed, true, true) : hd::quic_send_permission{{hd::quic_recovery_code::invalid}};
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
    pending_spaces_[0] = hd::quic_pn_space::application;
    debit_ = debit;
    pending_budget_ = std::move(budget);
    pending_bytes_ = output.bytes.size();
    return output;
}
void connection::emitted(bool success, clock_type::time_point now) {
    if (pending_control_) {
        require(static_cast<bool>(identity_->complete_control(pending_control_->token, success ? hd::quic_identity_emission::sent : hd::quic_identity_emission::unsent, now,
                                                              keys_.generation(hd::quic_key_direction::write))),
                "path actual send");
        if (success) ++stats_.emitted;
        pending_control_.reset();
        return;
    }
    require(pending_.has_value(), "send without preparation");
    if (success) {
        require(pending_budget_->complete_send(*debit_), "actual amplification debit");
        const auto committed = pending_batch_ ? recovery_.commit_datagram(std::span(pending_packets_).first(pending_count_), now) :
            recovery_.commit_sent(pending_->token, now, pending_bytes_, true, true, keys_.generation(hd::quic_key_direction::write));
        require(static_cast<bool>(committed), "actual UDP send commit");
        for (std::size_t i = 0; i < pending_count_; ++i) {
            const auto& plan = pending_plans_[i];
            if (plan.stream) require(static_cast<bool>(streams_.at(plan.stream->stream)->record_stream_sent(plan.stream->offset, plan.stream->data.size(), plan.stream->fin)), "stream actual send");
            if (plan.reset) require(static_cast<bool>(streams_.at(plan.reset->stream)->record_reset_sent(*plan.reset)), "reset actual send");
            if (plan.flow) flow_->credit_emitted(*plan.flow);
            auto index = static_cast<unsigned>(pending_spaces_[i]);
            if (pending_batch_ && probes_[index] && plan.ack_eliciting) --probes_[index];
        }
        ++stats_.emitted;
    } else {
        if (pending_batch_) recovery_.abandon_datagram();
        else recovery_.abandon_packet(pending_->token);
        pending_budget_->cancel_unsent(*debit_);
    }
    pending_count_ = 0;
    pending_.reset();
    debit_.reset();
    pending_budget_.reset();
}
std::optional<clock_type::time_point> connection::deadline() const {
    if (!failure_.empty()) return {};
    auto deadline = recovery_.next_deadline();
    auto due = recovery_.next_send_deadline();
    auto identity_due = identity_->deadline();
    if (identity_due && (!due || *identity_due < *due)) due = identity_due;
    if (deadline && (!due || deadline->deadline < *due)) due = deadline->deadline;
    if (!due || expires_ < *due) due = expires_;
    return due;
}
}  // namespace h3net
