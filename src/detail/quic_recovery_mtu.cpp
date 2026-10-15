/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/quic_recovery_internal.hpp>
namespace httpserver::detail {
quic_recovery_result quic_recovery::set_peer_max_datagram_size(std::uint64_t bytes) {
    if (bytes < 1200 || bytes > k_quic_max_integer) return {quic_recovery_code::invalid};
    if (preparation_pending() || impl_->mtu.deadline()) return {quic_recovery_code::busy};
    impl_->mtu.set_peer(bytes);
    return {};
}
std::size_t quic_recovery::path_datagram_size() const { return impl_->mtu.confirmed(); }
quic_send_plan quic_recovery::prepare_pmtu_probe(std::span<std::byte> output, time_point now, std::size_t overhead) {
    if (preparation_pending()) return {{quic_recovery_code::busy}};
    const auto size = impl_->mtu.candidate(now);
    if (!size || !impl_->environment.handshake_confirmed || !impl_->environment.peer_validated_endpoint)
        return {{quic_recovery_code::unavailable}};
    if (overhead >= size || size - overhead < 4) return {{quic_recovery_code::invalid}};
    if (output.size() < size - overhead) return {{quic_recovery_code::no_space}};
    auto permission = impl_->permission(now, size, true, true, false, quic_pn_space::application);
    if (!permission) return {{permission.code}};
    auto plan = reserve(quic_pn_space::application, true);
    if (!plan) return plan;
    plan.bytes = size - overhead;
    plan.ack_eliciting = true;
    std::fill_n(output.begin(), plan.bytes, std::byte{});
    output[0] = std::byte{1};
    impl_->pending->prepared = impl_->pending->ack_eliciting = impl_->pending->pmtu_probe = true;
    impl_->pending->payload_bytes = plan.bytes;
    return admit_scheduled(plan, now, {size, overhead});
}
void quic_recovery::implementation::sync_mtu(time_point now) {
    const auto size = mtu.confirmed();
    congestion.set_datagram_size(size);
    pacing.set_datagram_size(size, now, congestion.snapshot().window, rtt.smoothed);
    for (auto& entry : streams) entry.deficit = std::min(entry.deficit, size);
}
void quic_recovery::implementation::failed_mtu_opportunity(quic_pn_space space, time_point now) {
    std::optional<std::uint64_t> id;
    for (const auto& p : state(space)->sent) {
        if (p.path_epoch == path_epoch && p.in_flight && !p.pmtu_probe && !p.datagram_failed && !p.datagram_delivered && p.datagram_bytes > 1200) {
            id = p.datagram_id;
            break;
        }
    }
    if (!id) return;
    for (const auto& s : spaces) for (const auto& p : s.sent)
        if (p.datagram_id == *id && p.acknowledged) return;
    for (auto& s : spaces) for (auto& p : s.sent)
        if (p.datagram_id == *id) p.datagram_failed = true;
    if (!mtu.failed_datagram(recovery_after(now, recovery_scale(pto_duration(quic_pn_space::application), 10)))) return;
    sync_mtu(now);
    // A fallback invalidates discovery in progress and releases its flight once.
    for (auto& s : spaces) for (auto& p : s.sent) {
        if (p.path_epoch != path_epoch || !p.pmtu_probe || !p.in_flight) continue;
        in_flight -= p.wire_bytes;
        p.in_flight = false;
        p.lost = true;
    }
}
std::size_t quic_recovery::implementation::expire_mtu(time_point now) {
    auto id = mtu.expire(now, recovery_after(now, recovery_scale(pto_duration(quic_pn_space::application), 10)));
    if (!id) return 0;
    std::size_t lost = 0;
    for (auto& s : spaces) for (auto& p : s.sent) {
        if (!p.pmtu_probe || p.sequence != *id || !p.in_flight) continue;
        lost += p.wire_bytes;
        in_flight -= p.wire_bytes;
        p.in_flight = false;
        p.lost = true;
    }
    return lost;
}
}  // namespace httpserver::detail
