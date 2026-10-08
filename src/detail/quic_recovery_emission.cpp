/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
quic_recovery_code quic_recovery::implementation::check_flow() const {
    const auto& p = *pending;
    if (!p.flow) return quic_recovery_code::ok;
    quic_flow_result checked;
    if (p.stream) {
        const auto& f = *p.stream;
        checked = p.flow->check_stream_sent(f.stream, f.offset, f.data.size(), f.fin);
    } else if (p.reset) {
        checked = p.flow->check_reset_sent(p.reset->stream, p.reset->final_size);
    }
    return checked ? quic_recovery_code::ok : quic_recovery_code::invalid;
}
quic_recovery_code quic_recovery::implementation::charge_flow() {
    auto& pending = *this->pending;
    if (pending.flow) {
        quic_flow_result charged;
        if (pending.stream) {
            const auto& f = *pending.stream;
            charged = pending.flow->record_stream_sent(f.stream, f.offset, f.data.size(), f.fin);
        } else if (pending.reset) {
            charged = pending.flow->record_reset_sent(pending.reset->stream, pending.reset->final_size);
        }
        if (!charged) return quic_recovery_code::invalid;
    }
    return quic_recovery_code::ok;
}
void quic_recovery::implementation::record_emission(time_point now, std::size_t wire_bytes, bool eliciting, bool flight, std::uint64_t generation) {
    auto& pending = *this->pending;
    auto& s = *state(pending.space);
    implementation::packet packet;
    packet.critical = pending.critical;
    packet.sampled = rtt.sampled;
    packet.limited = pending.scheduled && pending.request.limited;
    packet.number = pending.number;
    packet.sequence = pending.token;
    packet.content = pending.content;
    packet.receive_watermark = pending.receive_watermark;
    packet.ack_generation = pending.ack_generation;
    packet.key_generation = generation;
    packet.sent_at = now;
    packet.wire_bytes = wire_bytes;
    packet.ack_eliciting = eliciting;
    packet.in_flight = flight;
    s.sent.push_back(packet);
    note_congestion_packet(packet, pending.space);
    s.last_sent = now;
    if (eliciting) s.last_eliciting = now;
    if (flight) in_flight += wire_bytes;
    update_information(packet.content, information_status::sent);
    if (pending.scheduled && in_flight < congestion.snapshot().window && !select_stream(false, pending.flow))
        s.sent.back().limited = true;
    commit_schedule();
}
void quic_recovery::implementation::finish_loss(std::optional<time_point> newest, time_point now) {
    compact_congestion_runs();
    if (!newest) return;
    bool persistent = persistent_congestion();
    congestion.lose(*newest, now, persistent);
    if (persistent) persistent_through = congestion_runs.back().last;
}
}  // namespace httpserver::detail
