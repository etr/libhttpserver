/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <iterator>
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
quic_send_plan quic_recovery::reserve_packet(quic_pn_space space) { return reserve(space, false); }
quic_send_plan quic_recovery::reserve(quic_pn_space space, bool critical) {
    auto* s = impl_->state(space);
    auto admission = impl_->send_admission(space);
    if (admission != quic_recovery_code::ok) return {{admission}};
    impl_->collect_packets(*s);
    if (s->sent.size() >= impl_->config.max_sent_packets) return {{quic_recovery_code::capacity}};
    if (space == quic_pn_space::application && !critical) {
        const auto count = std::count_if(s->sent.begin(), s->sent.end(), [](const auto& p) { return !p.critical; });
        if (static_cast<std::size_t>(count) >= impl_->config.max_sent_packets - impl_->config.critical_sent_packets) return {{quic_recovery_code::capacity}};
    }
    impl_->pending = implementation::preparation{space, impl_->next_token++, s->next_number++};
    impl_->pending->critical = critical;
    return {{}, impl_->pending->token, impl_->pending->number};
}
void quic_recovery::implementation::collect_packets(space_state& s) {
    auto last = std::find_if(s.sent.begin(), s.sent.end(), [&](const auto& p) { return !can_collect(p); });
    if (last != s.sent.begin()) s.sent_floor = std::prev(last)->number + 1;
    s.sent.erase(s.sent.begin(), last);
    std::erase_if(s.retired_sent, [&](auto r) { return r.largest < s.sent_floor; });
    for (auto& r : s.retired_sent) r.smallest = std::max(r.smallest, s.sent_floor);
    for (auto it = s.sent.begin(); it != s.sent.end();) {
        if (it->critical && can_collect(*it) && retire_packet_number(s, it->number)) it = s.sent.erase(it);
        else ++it;
    }
}
bool quic_recovery::implementation::retire_packet_number(space_state& s, std::uint64_t number) {
    auto pos = std::find_if(s.retired_sent.begin(), s.retired_sent.end(), [number](auto r) { return r.largest + 1 >= number; });
    if (pos != s.retired_sent.end() && number + 1 >= pos->smallest) {
        pos->smallest = std::min(pos->smallest, number);
        pos->largest = std::max(pos->largest, number);
    } else {
        if (s.retired_sent.size() == config.max_sent_packets) return false;
        pos = s.retired_sent.insert(pos, {number, number});
    }
    auto next = std::next(pos);
    if (next != s.retired_sent.end() && pos->largest + 1 >= next->smallest) {
        pos->largest = next->largest;
        s.retired_sent.erase(next);
    }
    return true;
}

quic_recovery_code quic_recovery::implementation::send_admission(quic_pn_space space) const {
    const auto* s = state(space);
    if (!s) return quic_recovery_code::invalid;
    if (s->discarded) return quic_recovery_code::discarded;
    if (admission != quic_recovery_code::ok) return admission;
    if (pending) return quic_recovery_code::busy;
    if (s->next_number > k_quic_max_integer || !next_token) return quic_recovery_code::capacity;
    return quic_recovery_code::ok;
}
quic_recovery_code quic_recovery::implementation::commit_admission(std::uint64_t token, time_point now, std::size_t bytes, bool eliciting, bool flight) const {
    if (!pending || pending->token != token) return quic_recovery_code::invalid;
    if (!valid_emission(bytes, eliciting, flight)) return quic_recovery_code::invalid;

    for (const auto& other : spaces)
        if (other.last_sent && now < *other.last_sent) return quic_recovery_code::invalid;
    return quic_recovery_code::ok;
}
bool quic_recovery::implementation::valid_emission(std::size_t bytes, bool eliciting, bool flight) const {
    if (!bytes || bytes > 65535 || (eliciting && !flight)) return false;
    return !pending->prepared || (pending->ack_eliciting == eliciting && bytes >= pending->payload_bytes);
}
quic_recovery_result quic_recovery::abandon_packet(std::uint64_t token) {
    if (!impl_->pending || impl_->pending->token != token) return {quic_recovery_code::invalid};
    impl_->pending.reset();
    return {};
}
quic_recovery_result quic_recovery::commit_sent(std::uint64_t token, time_point now, std::size_t wire_bytes,
                                              bool eliciting, bool in_flight, std::uint64_t generation) {
    auto admission = impl_->commit_admission(token, now, wire_bytes, eliciting, in_flight);
    if (admission != quic_recovery_code::ok) return {admission};
    if (impl_->pending->scheduled) {
        auto permission = check_scheduled_emission(token, now, wire_bytes, eliciting, in_flight);
        if (!permission) return {permission.code};
    }
    auto charged = impl_->charge_flow();
    if (charged != quic_recovery_code::ok) return {charged};
    impl_->record_emission(now, wire_bytes, eliciting, in_flight, generation);
    if (impl_->pending->ack_generation) publish_ack(impl_->pending->space, *impl_->pending->ack_generation);
    impl_->pending.reset();
    return {};
}
namespace {
using ack_ranges = std::array<quic_ack_range, 257>;
bool decode_ranges(const quic_ack_frame& ack, ack_ranges& ranges) {
    if (ack.largest > k_quic_max_integer || ack.delay > k_quic_max_integer || ack.range_count >= ranges.size()) return false;
    if (ack.ecn)
        for (auto count : *ack.ecn) if (count > k_quic_max_integer) return false;
    quic_ack_cursor cursor;
    for (std::size_t i = 0; i <= ack.range_count; ++i) {
        auto decoded = next_quic_ack_range(ack, cursor);
        if (decoded.code != quic_codec_code::ok) return false;
        ranges[i] = decoded.value;
    }
    return cursor.offset == ack.encoded_ranges.size();
}
bool covers(std::span<const quic_ack_range> ranges, std::uint64_t number) {
    return std::any_of(ranges.begin(), ranges.end(), [number](auto r) { return number >= r.smallest && number <= r.largest; });
}
template<class State>
std::size_t retired_count(const State& s, std::uint64_t lower, std::uint64_t upper) {
    std::size_t found = 0;
    for (auto retired : s.retired_sent) {
        const auto begin = std::max(lower, retired.smallest), end = std::min(upper, retired.largest);
        if (begin <= end) found += end - begin + 1;
    }
    return found;
}
template<class State>
bool acknowledges_sent(const State& s, std::span<const quic_ack_range> ranges, quic_recovery::time_point now) {
    for (auto range : ranges) {
        auto lower = std::max(range.smallest, s.sent_floor);
        if (lower > range.largest) continue;
        std::size_t found = 0;
        for (auto& p : s.sent) {
            if (p.number < lower || p.number > range.largest) continue;
            if (!p.acknowledged && now < p.sent_at) return false;
            ++found;
        }
        found += retired_count(s, lower, range.largest);
        if (found != range.largest - lower + 1) return false;
    }
    return true;
}
}  // namespace
quic_recovery_events quic_recovery::receive_ack(quic_pn_space space, const quic_ack_frame& ack, time_point now) {
    auto* s = impl_->state(space);
    if (!s) return {{quic_recovery_code::invalid}};
    if (s->discarded) return {{quic_recovery_code::discarded}};
    ack_ranges storage;
    if (!decode_ranges(ack, storage)) return {{quic_recovery_code::invalid}};
    auto ranges = std::span(storage).first(ack.range_count + 1);
    if (!acknowledges_sent(*s, ranges, now)) return {{quic_recovery_code::invalid}};
    impl_->acknowledge_congestion_runs(space, ranges);
    impl_->send_due.reset();
    impl_->sample_ack(*s, ranges, ack, now, space);
    auto result = impl_->acknowledge_packets(*s, ranges, space);
    if (!result.acknowledged_packets) return result;
    s->largest_acked = std::max(s->largest_acked.value_or(0), ack.largest);
    result.lost_bytes = impl_->detect_loss(*s, now);
    if (space == quic_pn_space::handshake) impl_->environment.peer_validated_endpoint = true;
    if (impl_->environment.peer_validated_endpoint) {
        impl_->probe_grants.fill(0);
        impl_->pto_count = 0;
        impl_->last_pto.reset();
        impl_->idle_pto_anchor.reset();
    }
    return result;
}
bool quic_recovery::implementation::sample_ack(const space_state& s, std::span<const quic_ack_range> ranges, const quic_ack_frame& ack, time_point now, quic_pn_space space) {
    bool newly_eliciting = false, newly_acknowledged = false;
    std::optional<time_point> sample_sent;
    for (const auto& p : s.sent) {
        if (p.acknowledged || !covers(ranges, p.number)) continue;
        newly_acknowledged = true;
        newly_eliciting |= p.ack_eliciting;
        if (p.number == ack.largest) sample_sent = p.sent_at;
    }
    if (!newly_acknowledged) return false;
    if (sample_sent && newly_eliciting) update_rtt(recovery_elapsed(now, *sample_sent), ack.delay, space);
    return newly_eliciting;
}
quic_recovery_events quic_recovery::implementation::acknowledge_packets(space_state& s, std::span<const quic_ack_range> ranges, quic_pn_space space) {
    quic_recovery_events result;
    for (auto& p : s.sent) {
        if (p.acknowledged || !covers(ranges, p.number)) continue;
        update_information(p.content, information_status::delivered);
        retire_acknowledged_receive(s, p);
        note_congestion_outcome(p, outcome::acknowledged);
        p.acknowledged = true;
        ++result.acknowledged_packets;
        if (p.in_flight) {
            congestion.acknowledge(p.wire_bytes, p.sent_at, p.limited);
            result.acknowledged_bytes += p.wire_bytes;
            in_flight -= p.wire_bytes;
            p.in_flight = false;
        }
        if (space == quic_pn_space::application)
            result.application_generation = std::max(result.application_generation.value_or(0), p.key_generation);
    }
    return result;
}
void quic_recovery::implementation::update_rtt(recovery_duration sample, std::uint64_t delay, quic_pn_space space) {
    rtt.latest = sample;
    if (!rtt.sampled) {
        rtt.sampled = true;
        rtt.minimum = rtt.smoothed = sample;
        rtt.variation = sample / 2;
        return;
    }
    rtt.minimum = std::min(rtt.minimum, sample);
    recovery_duration peer_delay{};
    if (space == quic_pn_space::application && environment.handshake_confirmed) {
        const auto cap = static_cast<std::uint64_t>(config.peer_max_ack_delay.count());
        const auto decoded = delay > (cap >> config.peer_ack_delay_exponent) ? cap : delay << config.peer_ack_delay_exponent;
        peer_delay = std::chrono::microseconds(decoded);
    }
    if (sample - rtt.minimum >= peer_delay) sample -= peer_delay;
    auto difference = sample > rtt.smoothed ? sample - rtt.smoothed : rtt.smoothed - sample;
    rtt.variation = recovery_average(rtt.variation, difference, 4);
    rtt.smoothed = recovery_average(rtt.smoothed, sample, 8);
}
std::size_t quic_recovery::implementation::detect_loss(space_state& s, time_point now) {
    s.loss_due.reset();
    if (!s.largest_acked) return 0;
    auto delay = std::max(recovery_scale(std::max(rtt.latest, rtt.smoothed), 9, 8), recovery_duration(std::chrono::milliseconds(1)));
    std::size_t lost = 0;
    std::optional<time_point> newest;
    for (auto& p : s.sent) {
        if (!p.in_flight || p.number > *s.largest_acked) continue;
        auto deadline = recovery_after(p.sent_at, delay);
        if (*s.largest_acked - p.number >= 3 || deadline <= now) {
            newest = newest ? std::max(*newest, p.sent_at) : p.sent_at;
            lost += p.wire_bytes;
            in_flight -= p.wire_bytes;
            p.in_flight = false;
            p.lost = true;
            note_congestion_outcome(p, outcome::lost);
            update_information(p.content, information_status::pending);
        } else if (!s.loss_due || deadline < *s.loss_due) {
            s.loss_due = deadline;
        }
    }
    finish_loss(newest, now);
    return lost;
}
quic_rtt_state quic_recovery::rtt() const { return impl_->rtt; }

std::size_t quic_recovery::bytes_in_flight() const { return impl_->in_flight; }
}  // namespace httpserver::detail
