/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
quic_recovery_result quic_recovery::begin_datagram() {
    if (preparation_pending()) return {quic_recovery_code::busy};
    if (impl_->admission != quic_recovery_code::ok) return {impl_->admission};
    impl_->datagram_active = true;
    return {};
}
quic_send_plan quic_recovery::prepare_datagram_packet(quic_pn_space space, std::span<std::byte> output, time_point now,
                                                     quic_send_request request, quic_flow_control& flow) {
    if (!impl_->datagram_active || impl_->pending) return {{quic_recovery_code::invalid}};
    const auto count = impl_->datagram_count;
    if (!impl_->state(space) || count == 3 || (count && space <= impl_->datagram[count - 1]->space)) return {{quic_recovery_code::invalid}};
    impl_->datagram_staging = true;
    auto plan = prepare_scheduled_packet(space, output, now, request, flow);
    impl_->datagram_staging = false;
    if (plan) {
        impl_->datagram[count] = impl_->pending;
        impl_->pending.reset();
        ++impl_->datagram_count;
    }
    return plan;
}
quic_send_permission quic_recovery::check_datagram_emission(std::span<const quic_datagram_emission> packets, time_point now) const {
    if (!impl_->datagram_active || impl_->pending || packets.empty() || packets.size() != impl_->datagram_count) return {{quic_recovery_code::invalid}};
    std::size_t bytes = 0, flight = 0;
    bool critical = true, pto = true, initial_minimum = false;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto& actual = packets[i];
        const auto& staged = *impl_->datagram[i];
        if (actual.wire_bytes > impl_->mtu.confirmed() - bytes) return {{quic_recovery_code::invalid}};
        if (impl_->state(staged.space)->discarded) return {{quic_recovery_code::discarded}};
        impl_->pending = staged;
        auto permission = check_scheduled_emission(actual.token, now, actual.wire_bytes, actual.ack_eliciting, actual.in_flight);
        impl_->pending.reset();
        if (!permission) return permission;
        bytes += actual.wire_bytes;
        if (actual.in_flight) {
            flight += actual.wire_bytes;
            critical &= staged.critical;
            pto &= staged.request.probe;
        }
        initial_minimum |= staged.space == quic_pn_space::initial && (impl_->config.role == quic_endpoint_role::client || actual.ack_eliciting);
    }
    if (initial_minimum && bytes < 1200) return {{quic_recovery_code::invalid}};
    auto permission = impl_->permission(now, flight, flight != 0, critical, pto && flight != 0, impl_->datagram[packets.size() - 1]->space);
    impl_->send_due = permission.deadline;
    return permission;
}
quic_recovery_result quic_recovery::commit_datagram(std::span<const quic_datagram_emission> packets, time_point now) {
    auto permission = check_datagram_emission(packets, now);
    if (!permission) return {permission.code};
    impl_->emitting_datagram = impl_->datagram[0]->token;
    impl_->emitting_bytes = 0;
    for (auto actual : packets) impl_->emitting_bytes += actual.wire_bytes;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto& actual = packets[i];
        impl_->pending = impl_->datagram[i];
        impl_->charge_flow();
        impl_->record_emission(now, actual.wire_bytes, actual.ack_eliciting, actual.in_flight, actual.key_generation);
        if (impl_->pending->ack_generation) publish_ack(impl_->pending->space, *impl_->pending->ack_generation);
        impl_->pending.reset();
    }
    impl_->emitting_datagram = impl_->emitting_bytes = 0;
    return abandon_datagram();
}
quic_recovery_result quic_recovery::abandon_datagram() {
    if (!impl_->datagram_active || impl_->pending) return {quic_recovery_code::invalid};
    impl_->datagram.fill({});
    impl_->datagram_count = 0;
    impl_->datagram_active = false;
    return {};
}
}  // namespace httpserver::detail
