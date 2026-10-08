/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_DETAIL_QUIC_RECOVERY_INTERNAL_HPP_
#define SRC_DETAIL_QUIC_RECOVERY_INTERNAL_HPP_
#include <algorithm>
#include <array>
#include <vector>
#include <limits>
#include <type_traits>
#include <httpserver/detail/quic_recovery.hpp>
namespace httpserver::detail {
using recovery_duration = std::chrono::steady_clock::duration;
inline recovery_duration recovery_scale(recovery_duration value, std::uint64_t numerator, std::uint64_t denominator = 1) {
    const auto maximum = static_cast<std::uint64_t>(recovery_duration::max().count());
    const auto count = static_cast<std::uint64_t>(value.count());
    const auto quotient = count / denominator, remainder = count % denominator;
    if (quotient > maximum / numerator) return recovery_duration::max();
    const auto extra = remainder * numerator / denominator;
    if (quotient * numerator > maximum - extra) return recovery_duration::max();
    return recovery_duration(quotient * numerator + extra);
}
inline recovery_duration recovery_elapsed(quic_recovery::time_point now, quic_recovery::time_point before) {
    if (now <= before) return {};
    using unsigned_rep = std::make_unsigned_t<recovery_duration::rep>;
    auto difference = static_cast<unsigned_rep>(now.time_since_epoch().count()) - static_cast<unsigned_rep>(before.time_since_epoch().count());
    return recovery_duration(std::min(difference, static_cast<unsigned_rep>(recovery_duration::max().count())));
}
inline quic_recovery::time_point recovery_after(quic_recovery::time_point now, recovery_duration delay) {
    const auto current = now.time_since_epoch().count();
    if (current > recovery_duration::max().count() - delay.count()) return quic_recovery::time_point::max();
    return now + delay;
}
inline recovery_duration recovery_sum(recovery_duration a, recovery_duration b) {
    if (a > recovery_duration::max() - b) return recovery_duration::max();
    return a + b;
}
inline recovery_duration recovery_average(recovery_duration old, recovery_duration sample, std::uint64_t denominator) {
    auto a = old.count(), b = sample.count();
    return recovery_duration((a / denominator) * (denominator - 1) + b / denominator +
        ((a % denominator) * (denominator - 1) + b % denominator) / denominator);
}
enum class information_status : std::uint8_t { pending, sent, delivered };
struct quic_recovery::implementation {
    struct information {
        quic_information_id id = 0;
        quic_information_kind kind = quic_information_kind::crypto;
        quic_pn_space space = quic_pn_space::initial;
        std::uint64_t stream = 0, offset = 0, error = 0;
        std::size_t length = 0, delivered_bytes = 0;
        std::vector<std::byte> data;
        std::vector<information_status> status;
        server::reservation storage;
        information_status terminal = information_status::delivered;
        bool fin = false, completed = false, cancelled = false, completion_pending = false;
    };
    struct slice {
        quic_information_id id = 0;
        std::size_t start = 0, length = 0;
        bool terminal = false;
    };
    struct packet {
        std::uint64_t number = 0, key_generation = 0;
        slice content;
        std::optional<std::uint64_t> receive_watermark, ack_generation;
        time_point sent_at{};
        std::size_t wire_bytes = 0;
        bool ack_eliciting = false, in_flight = false, acknowledged = false, lost = false;
    };
    struct space_state {
        std::vector<quic_ack_range> received;
        std::vector<packet> sent;
        std::uint64_t next_number = 0, sent_floor = 0;
        std::optional<std::uint64_t> largest_acked;
        std::optional<time_point> last_sent, last_eliciting, loss_due;
        std::uint64_t receive_floor = 0, ack_generation = 0, last_reordered_generation = 0;
        time_point largest_received_at{};
        std::optional<time_point> ack_due;
        unsigned eliciting_since_ack = 0;
        bool discarded = false;
    };
    quic_recovery_config config;
    server::resource_budget budget;
    server::reservation metadata;
    quic_recovery_code admission = quic_recovery_code::ok;
    std::array<space_state, 3> spaces;
    struct preparation {
        quic_pn_space space;
        std::uint64_t token, number;
        slice content;
        std::size_t payload_bytes = 0;
        bool prepared = false, ack_eliciting = false;
        std::optional<std::uint64_t> ack_generation, receive_watermark;
    };
    std::optional<preparation> pending;
    std::uint64_t next_token = 1, next_information = 1;
    std::vector<information> information_records;
    std::size_t retained_bytes = 0;
    information* find_information(quic_information_id id);
    const information* find_information(quic_information_id id) const;
    quic_information_result retain(information value, std::span<const std::byte> data);
    bool information_capacity() const;
    bool valid_information(const information& value, std::span<const std::byte> data) const;
    quic_encode_result encode_content(std::optional<slice>& content, std::span<std::byte> output);
    bool valid_emission(std::size_t bytes, bool eliciting, bool flight) const;
    quic_information_result store_information(information value, std::span<const std::byte> data);
    void update_information(slice content, information_status status);
    void complete_information(information& value);
    void release_storage(information& value);
    std::optional<slice> select_information(quic_pn_space space, bool probe) const;
    quic_frame information_frame(const information& value, slice content) const;
    quic_encode_result encode_information(slice& content, std::span<std::byte> output);
    void retire_acknowledged_receive(space_state& s, const packet& p);
    void retire_received(space_state& s, std::uint64_t watermark);
    bool can_collect(const packet& p) const;
    std::size_t in_flight = 0;
    quic_rtt_state rtt;
    quic_recovery_environment environment;
    unsigned pto_count = 0;
    std::optional<time_point> idle_pto_anchor, last_pto;
    std::optional<quic_recovery_timer> recovery_timer() const;
    std::array<bool, 3> due_acks(time_point now) const;
    std::optional<quic_recovery_timer> loss_timer() const;
    std::optional<quic_recovery_timer> probe_timer() const;
    recovery_duration pto_duration(quic_pn_space space) const;
    void repeat_receipt(space_state& s, quic_receipt receipt, bool eliciting, time_point now);
    void insert_received(space_state& s, std::uint64_t number);
    void note_ack_work(space_state& s, quic_pn_space space, bool gap, bool reordered, time_point now);
    void schedule_ack(space_state& s, quic_pn_space space, bool gap, time_point now);
    void collect_packets(space_state& s);
    quic_recovery_code send_admission(quic_pn_space space) const;
    quic_recovery_code commit_admission(std::uint64_t token, time_point now, std::size_t bytes, bool eliciting, bool flight) const;
    bool sample_ack(const space_state& s, std::span<const quic_ack_range> ranges, const quic_ack_frame& ack, time_point now, quic_pn_space space);
    quic_recovery_events acknowledge_packets(space_state& s, std::span<const quic_ack_range> ranges, quic_pn_space space);
    std::optional<quic_recovery_timer> space_probe_timer(quic_pn_space space) const;
    std::optional<quic_recovery_timer> idle_probe_timer() const;
    void update_rtt(std::chrono::steady_clock::duration sample, std::uint64_t delay, quic_pn_space space);
    std::size_t detect_loss(space_state& s, time_point now);
    implementation(quic_recovery_config config, server::resource_budget budget);
    space_state* state(quic_pn_space s) {
        auto index = static_cast<unsigned>(s);
        return index < spaces.size() ? &spaces[index] : nullptr;
    }
    const space_state* state(quic_pn_space s) const {
        auto index = static_cast<unsigned>(s);
        return index < spaces.size() ? &spaces[index] : nullptr;
    }
};
}  // namespace httpserver::detail
#endif  // SRC_DETAIL_QUIC_RECOVERY_INTERNAL_HPP_
