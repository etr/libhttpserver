/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <limits>
#include <httpserver/detail/quic_recovery_internal.hpp>
namespace httpserver::detail {
void quic_recovery::implementation::register_stream(std::uint64_t stream) {
    std::erase_if(streams, [&](auto entry) {
        return std::none_of(information_records.begin(), information_records.end(), [&](const auto& i) {
            return !i.critical && i.stream == entry.stream && !i.cancelled && !i.completed;
        });
    });
    auto found = std::find_if(streams.begin(), streams.end(), [stream](auto entry) { return entry.stream == stream; });
    if (found == streams.end()) streams.push_back({stream});
}
std::optional<quic_recovery::implementation::slice> quic_recovery::implementation::select_stream(bool probe, const quic_flow_control* flow) const {
    auto first = std::find_if(streams.begin(), streams.end(), [&](auto entry) { return entry.stream == next_stream; });
    auto start = first == streams.end() ? 0 : static_cast<std::size_t>(first - streams.begin());
    for (std::size_t n = 0; n < streams.size(); ++n) {
        const auto& entry = streams[(start + n) % streams.size()];
        if (auto content = stream_slice(entry, probe, flow)) return content;
    }
    return {};
}
std::optional<quic_recovery::implementation::slice> quic_recovery::implementation::stream_slice(const stream_schedule& entry, bool probe, const quic_flow_control* flow) const {
    for (const auto& value : information_records) {
        if (value.critical || value.stream != entry.stream) continue;
        if (value.completed || value.cancelled) continue;
        auto content = eligible_slice(value, probe, flow);
        if (!content) continue;
        auto credit = entry.deficit ? entry.deficit : mtu.confirmed();
        if (content->length > credit) {
            content->length = credit;
            content->terminal = false;
        }
        return content;
    }
    return {};
}
bool quic_recovery::implementation::data_window_available(std::size_t bytes) const {
    auto window = congestion.snapshot().window;
    auto available = window - std::min(config.control_reserve, window / 2);
    return in_flight <= available && bytes <= available - in_flight;
}
bool quic_recovery::implementation::prefer_data(quic_pn_space space, slice content, std::span<std::byte> output) {
    if (consecutive_controls < 2) return false;
    const auto& s = *state(space);
    auto data_packets = std::count_if(s.sent.begin(), s.sent.end(), [](const auto& p) { return !p.critical; });
    if (static_cast<std::size_t>(data_packets) >= config.max_sent_packets - config.critical_sent_packets) return false;
    if (!scheduled_now) return true;
    auto encoded = encode_information(content, output);
    if (encoded.code != quic_codec_code::ok) return false;
    return static_cast<bool>(permission(*scheduled_now, encoded.consumed + scheduled_overhead, true, false, false, space));
}
std::optional<quic_recovery::implementation::slice> quic_recovery::implementation::select_information(quic_pn_space space, bool probe, const quic_flow_control* flow, std::span<std::byte> output) {
    auto data = space == quic_pn_space::application ? select_stream(probe, flow) : std::nullopt;
    if (data && prefer_data(space, *data, output)) return data;
    for (const auto& value : information_records) {
        if (!value.critical || value.space != space || value.completed || value.cancelled) continue;
        if (auto content = eligible_slice(value, probe, flow)) return content;
    }
    return data;
}
quic_send_permission quic_recovery::implementation::permission(time_point now, std::size_t bytes, bool flight,
                                                               bool critical, bool probe, quic_pn_space space) const {
    const auto index = static_cast<unsigned>(space);
    if (!environment.send_permitted || !environment.write_keys[index]) return {{quic_recovery_code::unavailable}};
    if (probe && !probe_grants[index]) return {{quic_recovery_code::unavailable}};
    if (!flight || probe) return {};
    auto window = congestion.snapshot().window;
    auto permission = window_permission(bytes, critical);
    if (!permission) return permission;
    if (auto due = pacing.deadline(now, bytes, window, rtt.smoothed)) return {{quic_recovery_code::pacing_blocked}, due};
    return {};
}
quic_send_permission quic_recovery::implementation::window_permission(std::size_t bytes, bool critical) const {
    auto window = congestion.snapshot().window;
    if (in_flight > window || bytes > window - in_flight) return {{quic_recovery_code::congestion_blocked}};
    if (!critical && !data_window_available(bytes)) return {{quic_recovery_code::congestion_blocked}};
    return {};
}
quic_send_plan quic_recovery::prepare_scheduled_packet(quic_pn_space space, std::span<std::byte> output, time_point now,
                                                       quic_send_request request, quic_flow_control& flow) {
    if (!impl_->state(space) || !request.max_wire_bytes || request.max_wire_bytes > impl_->config.max_datagram_size ||
        request.protection_overhead >= request.max_wire_bytes) return {{quic_recovery_code::invalid}};
    if (impl_->pending || (impl_->datagram_active && !impl_->datagram_staging)) return {{quic_recovery_code::busy}};
    request.max_wire_bytes = std::min(request.max_wire_bytes, impl_->mtu.confirmed());
    if (request.protection_overhead >= request.max_wire_bytes) return {{quic_recovery_code::invalid}};
    auto payload = output.first(std::min(output.size(), request.max_wire_bytes - request.protection_overhead));
    impl_->scheduled_overhead = request.protection_overhead;
    impl_->scheduled_now = now;
    auto plan = prepare(space, payload, now, &flow, request.probe);
    impl_->scheduled_overhead = 0;
    impl_->scheduled_now.reset();
    plan = admit_scheduled(plan, now, request);
    const bool blocked = plan.code == quic_recovery_code::congestion_blocked || plan.code == quic_recovery_code::pacing_blocked;
    if (blocked && ack_deadline(space)) return admit_scheduled(prepare(space, payload, now, &flow, request.probe, true), now, request);
    return plan;
}
quic_send_plan quic_recovery::admit_scheduled(quic_send_plan plan, time_point now, quic_send_request request) {
    if (!plan) return plan;
    impl_->pending->scheduled = true;
    impl_->pending->request = request;
    auto permission = check_scheduled_emission(plan.token, now, plan.bytes + request.protection_overhead, plan.ack_eliciting, plan.ack_eliciting);
    impl_->send_due = permission.deadline;
    if (!permission) {
        abandon_packet(plan.token);
        quic_send_plan blocked{{permission.code}};
        blocked.deadline = permission.deadline;
        return blocked;
    }
    return plan;
}
quic_send_permission quic_recovery::check_scheduled_emission(std::uint64_t token, time_point now, std::size_t bytes, bool eliciting, bool flight) const {
    auto admission = impl_->commit_admission(token, now, bytes, eliciting, flight);
    if (admission != quic_recovery_code::ok) return {{admission}};
    const auto& p = *impl_->pending;
    if (!p.scheduled || bytes > p.request.max_wire_bytes || bytes < p.payload_bytes + p.request.protection_overhead)
        return {{quic_recovery_code::invalid}};
    const auto ceiling = p.path_epoch && *p.path_epoch != impl_->path_epoch ? std::size_t{1200} : impl_->mtu.confirmed();
    if (!p.pmtu_probe && bytes > ceiling) return {{quic_recovery_code::invalid}};
    if (!flight && bytes > p.payload_bytes + p.request.protection_overhead) return {{quic_recovery_code::invalid}};
    if (p.pmtu_probe && bytes != p.request.max_wire_bytes) return {{quic_recovery_code::invalid}};
    if (p.pmtu_probe && (!impl_->mtu.candidate(now) ||
                         !impl_->environment.handshake_confirmed || !impl_->environment.peer_validated_endpoint))
        return {{quic_recovery_code::unavailable}};
    auto checked = impl_->check_flow();
    if (checked != quic_recovery_code::ok) return {{checked}};
    // Candidate validation must not wait for ACKs stranded on the old path.
    // All off-path probes share a bounded initial window; identity additionally
    // enforces per-path amplification credit and finite challenge retries.
    if (p.path_epoch && *p.path_epoch == std::numeric_limits<std::uint64_t>::max()) {
        auto gate = impl_->permission(now, bytes, false, p.critical, p.request.probe, p.space);
        if (!gate) return gate;
        std::size_t candidate_flight = 0;
        for (const auto& packet : impl_->state(p.space)->sent)
            if (packet.in_flight && packet.path_epoch == *p.path_epoch) candidate_flight += packet.wire_bytes;
        const auto limit = quic_congestion(1200).snapshot().window;
        return candidate_flight <= limit && bytes <= limit - candidate_flight ? quic_send_permission{} : quic_send_permission{{quic_recovery_code::congestion_blocked}};
    }
    auto permission = impl_->permission(now, bytes, flight, p.critical, p.request.probe, p.space);
    impl_->send_due = permission.deadline;
    return permission;
}
void quic_recovery::implementation::commit_schedule() {
    const auto& p = *pending;
    const auto& packet = state(p.space)->sent.back();
    if (packet.in_flight) pacing.emitted(packet.sent_at, packet.wire_bytes, congestion.snapshot().window, rtt.smoothed);
    if (p.scheduled) {
        if (packet.ack_eliciting && p.request.probe) --probe_grants[static_cast<unsigned>(p.space)];
        send_due.reset();
    }
    if (p.stream && !p.critical) commit_stream_turn();
    else if (p.ack_eliciting || p.ack_generation) consecutive_controls = std::min(consecutive_controls + 1, 2U);
}
void quic_recovery::implementation::commit_stream_turn() {
    const auto& p = *pending;
    consecutive_controls = 0;
    auto entry = std::find_if(streams.begin(), streams.end(), [&](auto e) { return e.stream == p.stream->stream; });
    if (entry == streams.end()) return;
    auto credit = entry->deficit ? entry->deficit : mtu.confirmed();
    entry->deficit = credit - std::min(credit, std::max(std::size_t{1}, p.stream->data.size()));
    auto next = entry->deficit ? entry : std::next(entry);
    if (next == streams.end()) next = streams.begin();
    next_stream = next->stream;
}
quic_congestion_snapshot quic_recovery::congestion() const { return impl_->congestion.snapshot(); }
std::optional<quic_recovery::time_point> quic_recovery::next_send_deadline() const { return impl_->send_due; }
quic_send_plan quic_recovery::prepare_path_packet(const quic_path_frame& frame, std::span<std::byte> output, time_point now, quic_send_request request, bool on_active_path) {
    if (frame.data.size() != 8 || request.protection_overhead >= request.max_wire_bytes || request.max_wire_bytes > impl_->config.max_datagram_size) return {{quic_recovery_code::invalid}};
    request.max_wire_bytes = std::min(request.max_wire_bytes, on_active_path ? impl_->mtu.confirmed() : std::size_t{1200});
    if (request.protection_overhead >= request.max_wire_bytes) return {{quic_recovery_code::invalid}};
    auto plan = reserve(quic_pn_space::application, true);
    if (!plan) return plan;
    auto bytes = output.first(std::min(output.size(), request.max_wire_bytes - request.protection_overhead));
    auto encoded = encode_quic_frame(frame, bytes);
    if (encoded.code != quic_codec_code::ok) {
        abandon_packet(plan.token);
        return {{quic_recovery_code::no_space}};
    }
    std::fill(bytes.begin() + encoded.consumed, bytes.end(), std::byte{});
    plan.bytes = bytes.size();
    plan.ack_eliciting = true;
    impl_->pending->prepared = true;
    impl_->pending->ack_eliciting = true;
    impl_->pending->payload_bytes = plan.bytes;
    if (!on_active_path) impl_->pending->path_epoch = std::numeric_limits<std::uint64_t>::max();
    return admit_scheduled(plan, now, request);
}
}  // namespace httpserver::detail
