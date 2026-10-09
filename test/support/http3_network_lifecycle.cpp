/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <stdexcept>
#include <utility>
#include "test/support/http3_network_owner.hpp"
#include <httpserver/detail/quic_datagram_sizing.hpp>
namespace h3net {
void connection::request_close(const hd::quic_close_frame& frame, clock_type::time_point now) {
    if (lifecycle_.local_close(frame, now, recovery_.current_pto())) terminal_changed(now);
}
void connection::terminal_changed(clock_type::time_point now) {
    if (!terminal_stopped_) {
        terminal_stopped_ = true;
        ++close_notifications_;
        // State is committed before disconnect can make cancellation runnable.
        if (engine_) engine_->disconnect({httpserver::http::outcome_code::connection_closed, "QUIC connection closed"});
        identity_->stop();
        tls_.reset();
        probes_.fill(0);
    }
    if (lifecycle_.state() != hd::quic_lifecycle_state::closing) keys_.clear();
    retire_terminal(now);
}
void connection::retire_terminal(clock_type::time_point) {
    if (active() || receiving_ || pending_ || pending_control_ || close_pending_) return;
    if (!terminal_retired_) {
        if (lifecycle_.state() == hd::quic_lifecycle_state::closing) {
            // Cache only terminal frames, before dropping ordinary recovery.
            // A missing key or exhausted preparation fails silently.
            try {
                cache_close();
            } catch (...) {
                cached_close_.reset();
            }
            if (!connected_) keys_.discard_level(hd::quic_key_level::application);
        }
        for (unsigned i = 0; i < 3; ++i) recovery_.discard_space(static_cast<hd::quic_pn_space>(i));
        for (auto& ranges : crypto_acknowledged_) ranges.clear();
        engine_.reset();
        files_.reset();
        streams_.clear();
        flow_.reset();
        terminal_retired_ = true;
    }
    if (lifecycle_.state() != hd::quic_lifecycle_state::closing) {
        keys_.clear();
        cached_close_.reset();
    }
    if (lifecycle_.state() == hd::quic_lifecycle_state::closed) identity_->detach_routes();
}
void connection::cache_close() {
    constexpr std::size_t ceiling = 1472;
    std::array<std::array<std::byte, ceiling>, 3> plain{};
    std::array<hd::quic_datagram_packet, 3> packets{};
    std::array<hd::quic_key_level, 3> levels{};
    std::size_t count = 0;
    const auto destination = identity_->application_cid();
    for (unsigned i = 0; i < 3; ++i) {
        const auto level = static_cast<hd::quic_key_level>(i);
        if ((connected_ && i != 2) || (!connected_ && i == 2) || !keys_.keys(level, hd::quic_key_direction::write)) continue;
        auto plan = recovery_.reserve_packet(static_cast<hd::quic_pn_space>(i));
        if (!plan) continue;
        recovery_.abandon_packet(plan.token);  // Burn PN; cached close has no reliable information.
        hd::quic_packet_write write;
        write.kind = i == 0 ? hd::quic_packet_kind::initial : i == 1 ? hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
        write.packet_number_width = 4;
        write.packet_number = plan.packet_number;
        write.destination = std::span(destination.bytes).first(destination.size);
        if (i != 2) {
            write.source = server_cid_;
            write.length_width = 2;
        }
        auto frame = hd::encode_quic_frame(lifecycle_.close_frame(i == 2), plain[count], {write.kind, hd::quic_endpoint_role::server});
        if (frame.code != hd::quic_codec_code::ok) continue;
        // Apply the conservative Initial datagram floor even for terminal output.
        packets[count] = {write, std::max(frame.consumed, std::size_t{4}), i == 0};
        levels[count++] = level;
    }
    if (!count) return;
    auto layout = hd::size_quic_datagram(std::span(packets).first(count), hd::quic_endpoint_role::server, recovery_.path_datagram_size());
    if (layout.code != hd::quic_codec_code::ok) return;
    transmission output;
    output.peer = identity_->application_path().peer;
    output.bytes.resize(layout.total);
    std::array<std::byte, ceiling> scratch{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < count; ++i) {
        packets[i].header.payload = std::span(plain[i]).first(layout.payload[i]);
        auto result = keys_.protect_packet(levels[i], packets[i].header, std::span(output.bytes).subspan(at, layout.wire[i]), scratch);
        if (result.code != hd::quic_crypto_code::ok || result.consumed != layout.wire[i]) return;
        at += result.consumed;
    }
    cached_close_ = std::move(output);
}
std::optional<transmission> connection::prepare_close(clock_type::time_point now) {
    retire_terminal(now);
    if (!cached_close_ || !lifecycle_.response_due(now)) return {};
    auto budget = identity_->application_budget();
    // Copy before reserving so allocation failure cannot leak amplification debit.
    transmission output = *cached_close_;
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
}  // namespace h3net
