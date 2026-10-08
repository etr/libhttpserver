/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
namespace {
void earlier(std::optional<quic_recovery_timer>& timer, quic_recovery_timer candidate) {
    if (!timer || candidate.deadline < timer->deadline) timer = candidate;
}
template<class Space>
bool outstanding_eliciting(const Space& s) {
    return std::any_of(s.sent.begin(), s.sent.end(), [](const auto& p) { return p.in_flight && p.ack_eliciting; });
}
}  // namespace
recovery_duration quic_recovery::implementation::pto_duration(quic_pn_space space) const {
    auto variance = std::max(recovery_scale(rtt.variation, 4), recovery_duration(std::chrono::milliseconds(1)));
    auto duration = recovery_sum(rtt.smoothed, variance);
    if (space == quic_pn_space::application && environment.handshake_confirmed)
        duration = recovery_sum(duration, config.peer_max_ack_delay);
    for (unsigned i = 0; i < pto_count; ++i) duration = recovery_scale(duration, 2);
    return duration;
}
std::optional<quic_recovery_timer> quic_recovery::implementation::loss_timer() const {
    std::optional<quic_recovery_timer> result;
    for (std::size_t i = 0; i < spaces.size(); ++i)
        if (spaces[i].loss_due) earlier(result, {quic_recovery_timer::kind::detect_loss, static_cast<quic_pn_space>(i), *spaces[i].loss_due});
    return result;
}
std::optional<quic_recovery_timer> quic_recovery::implementation::space_probe_timer(quic_pn_space space) const {
    const auto i = static_cast<std::size_t>(space);
    const auto& s = spaces[i];
    if (s.discarded || !environment.write_keys[i] || !outstanding_eliciting(s)) return {};
    if (space == quic_pn_space::application && !environment.handshake_confirmed) return {};
    auto anchor = std::max(*s.last_eliciting, last_pto.value_or(*s.last_eliciting));
    return quic_recovery_timer{quic_recovery_timer::kind::probe, space, recovery_after(anchor, pto_duration(space))};
}
std::optional<quic_recovery_timer> quic_recovery::implementation::idle_probe_timer() const {
    if (environment.peer_validated_endpoint || !idle_pto_anchor) return {};
    for (auto space : {quic_pn_space::handshake, quic_pn_space::initial}) {
        const auto i = static_cast<std::size_t>(space);
        if (spaces[i].discarded || !environment.write_keys[i]) continue;
        auto anchor = std::max(*idle_pto_anchor, last_pto.value_or(*idle_pto_anchor));
        return quic_recovery_timer{quic_recovery_timer::kind::probe, space, recovery_after(anchor, pto_duration(space))};
    }
    return {};
}
std::optional<quic_recovery_timer> quic_recovery::implementation::probe_timer() const {
    if (!environment.send_permitted) return {};
    std::optional<quic_recovery_timer> result;
    bool any_outstanding = false;
    for (std::size_t i = 0; i < spaces.size(); ++i) {
        any_outstanding |= outstanding_eliciting(spaces[i]);
        auto timer = space_probe_timer(static_cast<quic_pn_space>(i));
        if (timer) earlier(result, *timer);
    }
    return any_outstanding ? result : idle_probe_timer();
}
std::optional<quic_recovery_timer> quic_recovery::implementation::recovery_timer() const {
    auto loss = loss_timer();
    return loss ? loss : probe_timer();
}
void quic_recovery::set_environment(quic_recovery_environment environment, time_point now) {
    if (impl_->config.role == quic_endpoint_role::server || environment.handshake_confirmed)
        environment.peer_validated_endpoint = true;
    impl_->environment = environment;
    if (environment.peer_validated_endpoint) impl_->idle_pto_anchor.reset();
    else if (!impl_->idle_pto_anchor) impl_->idle_pto_anchor = now;
}
std::optional<quic_recovery_timer> quic_recovery::next_deadline() const {
    auto result = impl_->recovery_timer();
    if (!impl_->environment.send_permitted) return result;
    for (std::size_t i = 0; i < impl_->spaces.size(); ++i) {
        const auto& s = impl_->spaces[i];
        if (s.ack_due && !s.discarded && impl_->environment.write_keys[i])
            earlier(result, {quic_recovery_timer::kind::acknowledge, static_cast<quic_pn_space>(i), *s.ack_due});
    }
    return result;
}
std::array<bool, 3> quic_recovery::implementation::due_acks(time_point now) const {
    std::array<bool, 3> result{};
    if (!environment.send_permitted) return result;
    for (std::size_t i = 0; i < spaces.size(); ++i) {
        const auto& s = spaces[i];
        result[i] = s.ack_due && *s.ack_due <= now && !s.discarded && environment.write_keys[i];
    }
    return result;
}
quic_recovery_events quic_recovery::expire(time_point now) {
    quic_recovery_events result;
    result.acknowledge_spaces = impl_->due_acks(now);
    auto timer = impl_->recovery_timer();
    if (!timer || timer->deadline > now) return result;
    if (timer->action == quic_recovery_timer::kind::detect_loss) {
        for (auto& s : impl_->spaces)
            if (s.loss_due && *s.loss_due <= now) result.lost_bytes += impl_->detect_loss(s, now);
    } else {
        result.probe_space = timer->space;
        result.probes = outstanding_eliciting(*impl_->state(timer->space)) ? 2 : 1;
        impl_->pto_count = std::min(impl_->pto_count + 1, 63U);
        impl_->last_pto = now;
    }
    return result;
}
unsigned quic_recovery::pto_count() const { return impl_->pto_count; }
quic_recovery_events quic_recovery::discard_space(quic_pn_space space) {
    auto* s = impl_->state(space);
    if (!s) return {{quic_recovery_code::invalid}};
    quic_recovery_events result;
    for (auto& p : s->sent)
        if (p.in_flight) result.discarded_bytes += p.wire_bytes;
    impl_->in_flight -= result.discarded_bytes;
    for (auto& value : impl_->information_records) {
        if (value.space != space) continue;
        value.cancelled = true;
        value.completion_pending = false;
        impl_->release_storage(value);
    }
    s->sent.clear();
    s->received.clear();
    s->loss_due.reset();
    s->ack_due.reset();
    s->discarded = true;
    if (impl_->pending && impl_->pending->space == space) impl_->pending.reset();
    impl_->pto_count = 0;
    impl_->last_pto.reset();
    return result;
}
}  // namespace httpserver::detail
