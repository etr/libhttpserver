/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
namespace {
bool valid_capacities(const quic_recovery_config& c) {
    return c.max_receive_ranges <= 256 && c.max_sent_packets <= 4096 && c.max_information <= 4096 && c.max_retained_bytes <= 16777216;
}
bool valid_ack_config(const quic_recovery_config& c) {
    return c.local_ack_delay_exponent <= 20 && c.peer_ack_delay_exponent <= 20 &&
        c.local_max_ack_delay.count() >= 0 && c.local_max_ack_delay < std::chrono::milliseconds(16384) &&
        c.peer_max_ack_delay.count() >= 0 && c.peer_max_ack_delay < std::chrono::milliseconds(16384);
}
}  // namespace
quic_recovery::implementation::implementation(quic_recovery_config c, server::resource_budget b) : config(c), budget(std::move(b)) {
    if (!valid_capacities(c) || !valid_ack_config(c)) throw std::invalid_argument("Invalid QUIC recovery limits");
    auto bytes = 3 * ((c.max_receive_ranges + 1) * sizeof(quic_ack_range) + c.max_sent_packets * sizeof(packet)) + c.max_information * sizeof(information);
    try {
        if (!budget.reserve(server::resource::quic_reassembly_bytes, bytes, metadata).ok()) {
            admission = quic_recovery_code::no_memory;
            return;
        }
        std::array<space_state, 3> staged;
        std::vector<information> descriptors;
        descriptors.reserve(c.max_information);
        for (auto& s : staged) {
            s.received.reserve(c.max_receive_ranges + 1);
            s.sent.reserve(c.max_sent_packets);
        }
        spaces = std::move(staged);
        information_records = std::move(descriptors);
    } catch (const std::bad_alloc&) {
        admission = quic_recovery_code::no_memory;
        metadata.release();
    }
}
quic_recovery::quic_recovery(quic_recovery_config c, server::resource_budget b)
    : impl_(std::make_unique<implementation>(c, std::move(b))) {}
quic_recovery::~quic_recovery() = default;
quic_receipt quic_recovery::inspect_received(quic_pn_space space, std::uint64_t number) const {
    auto* s = impl_->state(space);
    if (!s || number > k_quic_max_integer) return quic_receipt::invalid;
    if (s->discarded || number < s->receive_floor) return quic_receipt::retired;
    for (auto range : s->received)
        if (number >= range.smallest && number <= range.largest) return quic_receipt::duplicate;
    return quic_receipt::fresh;
}
quic_recovery_result quic_recovery::receive_packet(quic_pn_space space, std::uint64_t number, bool eliciting, time_point now) {
    auto receipt = inspect_received(space, number);
    if (receipt == quic_receipt::invalid) return {quic_recovery_code::invalid};
    if (receipt != quic_receipt::fresh) {
        impl_->repeat_receipt(*impl_->state(space), receipt, eliciting, now);
        return {};
    }
    if (impl_->admission != quic_recovery_code::ok) return {impl_->admission};
    if (!impl_->config.max_receive_ranges) return {quic_recovery_code::capacity};
    auto& s = *impl_->state(space);
    const auto expected = s.received.empty() ? s.receive_floor : s.received.front().largest + 1;
    const bool gap = number != expected;
    if (s.received.empty() || number > s.received.front().largest) s.largest_received_at = now;
    impl_->insert_received(s, number);
    if (eliciting) {
        impl_->note_ack_work(s, space, gap, number + 1 < expected, now);
    }
    return {};
}
void quic_recovery::implementation::repeat_receipt(space_state& s, quic_receipt receipt, bool eliciting, time_point now) {
    if (receipt == quic_receipt::duplicate && eliciting) {
        s.ack_due = now;
        ++s.ack_generation;
    }
}
void quic_recovery::implementation::insert_received(space_state& s, std::uint64_t number) {
    auto pos = std::find_if(s.received.begin(), s.received.end(), [number](auto r) { return r.largest < number; });
    s.received.insert(pos, {number, number});
    for (std::size_t i = 1; i < s.received.size();) {
        auto& high = s.received[i - 1];
        auto low = s.received[i];
        if (low.largest + 1 >= high.smallest) {
            high.smallest = low.smallest;
            s.received.erase(s.received.begin() + i);
        } else {
            ++i;
        }
    }
    if (s.received.size() > config.max_receive_ranges) {
        s.received.pop_back();
        s.receive_floor = s.received.back().smallest;
    }
}
void quic_recovery::implementation::note_ack_work(space_state& s, quic_pn_space space, bool gap, bool reordered, time_point now) {
    ++s.ack_generation;
    if (reordered) s.last_reordered_generation = s.ack_generation;
    schedule_ack(s, space, gap, now);
}
void quic_recovery::implementation::schedule_ack(space_state& s, quic_pn_space space, bool gap, time_point now) {
    ++s.eliciting_since_ack;
    if (space != quic_pn_space::application || gap || s.eliciting_since_ack >= 2) {
        s.ack_due = now;
    } else if (!s.ack_due) {
        s.ack_due = recovery_after(now, config.local_max_ack_delay);
    }
}
std::optional<quic_recovery::time_point> quic_recovery::ack_deadline(quic_pn_space space) const {
    auto* s = impl_->state(space);
    return s ? s->ack_due : std::nullopt;
}
quic_ack_plan quic_recovery::prepare_ack(quic_pn_space space, std::span<std::byte> output, time_point now) const {
    auto* s = impl_->state(space);
    if (!s) return {{quic_recovery_code::invalid}};
    if (s->discarded) return {{quic_recovery_code::discarded}};
    if (s->received.empty()) return {{quic_recovery_code::no_data}};
    std::uint64_t delay = 0;
    if (space == quic_pn_space::application && now > s->largest_received_at)
        delay = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(recovery_elapsed(now, s->largest_received_at)).count()) >> impl_->config.local_ack_delay_exponent;
    auto result = encode_quic_ack(s->received, std::min(delay, k_quic_max_integer), {}, output);
    if (result.code != quic_codec_code::ok) return {{quic_recovery_code::no_space}};
    return {{}, result.consumed, s->ack_generation};
}
void quic_recovery::publish_ack(quic_pn_space space, std::uint64_t generation) {
    auto* s = impl_->state(space);
    if (s && generation == s->ack_generation) {
        s->ack_due.reset();
        s->eliciting_since_ack = 0;
    }
}
}  // namespace httpserver::detail
