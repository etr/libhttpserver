/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_recovery_internal.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_RECOVERY_INTERNAL_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_RECOVERY_INTERNAL_HPP_
#include <algorithm>
#include <array>
#include <vector>
#include <limits>
#include <type_traits>
#include <httpserver/detail/quic_recovery.hpp>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_pacing.hpp>
#include <httpserver/detail/quic_path_mtu.hpp>
namespace httpserver::detail {
using recovery_duration = std::chrono::steady_clock::duration;
recovery_duration recovery_scale(recovery_duration value, std::uint64_t numerator, std::uint64_t denominator = 1);
recovery_duration recovery_elapsed(quic_recovery::time_point now, quic_recovery::time_point before);
quic_recovery::time_point recovery_after(quic_recovery::time_point now, recovery_duration delay);
recovery_duration recovery_sum(recovery_duration a, recovery_duration b);
recovery_duration recovery_average(recovery_duration old, recovery_duration sample, std::uint64_t denominator);
enum class information_status : std::uint8_t { pending, sent, delivered };
struct quic_recovery::implementation {
    struct information {
        quic_information_id id = 0;
        quic_information_kind kind = quic_information_kind::crypto;
        quic_pn_space space = quic_pn_space::initial;
        std::uint64_t stream = 0, offset = 0, error = 0;
        quic_flow_frame flow;
        std::array<std::byte, 36> cid_bytes{};
        std::size_t cid_size = 0;
        std::size_t length = 0, delivered_bytes = 0;
        std::vector<std::byte> data;
        std::vector<information_status> status;
        server::reservation storage;
        information_status terminal = information_status::delivered;
        bool critical = true, fin = false, completed = false, cancelled = false, completion_pending = false;
    };
    struct slice {
        quic_information_id id = 0;
        std::size_t start = 0, length = 0;
        bool terminal = false;
    };
    struct packet {
        std::uint64_t number = 0, key_generation = 0, sequence = 0, path_epoch = 0, datagram_id = 0;
        slice content{};
        std::optional<std::uint64_t> receive_watermark, ack_generation;
        time_point sent_at{};
        std::size_t wire_bytes = 0, datagram_bytes = 0;
        bool datagram_failed = false, datagram_delivered = false;
        bool sampled = false, limited = false, pmtu_probe = false;
        bool critical = false, ack_eliciting = false, in_flight = false, acknowledged = false, lost = false;
    };
    struct space_state {
        std::vector<quic_ack_range> received;
        std::vector<packet> sent;
        std::vector<quic_ack_range> retired_sent;
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
    server::resource_budget budget, critical_budget;
    server::reservation metadata;
    quic_recovery_code admission = quic_recovery_code::ok;
    std::array<space_state, 3> spaces;
    struct preparation {
        quic_pn_space space;
        std::uint64_t token, number;
        slice content{};
        std::size_t payload_bytes = 0;
        bool prepared = false, ack_eliciting = false, critical = false;
        quic_send_request request{};
        bool scheduled = false, pmtu_probe = false;
        std::optional<std::uint64_t> path_epoch;
        quic_flow_control* flow = nullptr;
        std::optional<quic_stream_frame> stream{};
        std::optional<quic_reset_stream_frame> reset{};
        std::optional<std::uint64_t> ack_generation{}, receive_watermark{};
    };
    std::optional<preparation> pending;
    std::array<std::optional<preparation>, 3> datagram{};
    std::size_t datagram_count = 0, emitting_bytes = 0;
    std::uint64_t emitting_datagram = 0;
    bool datagram_active = false, datagram_staging = false;
    std::uint64_t next_token = 1, next_information = 1;
    std::vector<information> information_records;
    std::size_t retained_bytes = 0, data_retained_bytes = 0;
    information* find_information(quic_information_id id);
    const information* find_information(quic_information_id id) const;
    quic_information_result retain(information value, std::span<const std::byte> data);
    bool information_capacity(bool critical) const;
    bool payload_capacity(bool critical, std::size_t bytes) const;
    bool valid_information(const information& value, std::span<const std::byte> data) const;
    quic_recovery_result stage_plan(quic_send_plan& plan, std::optional<slice> content, std::span<std::byte> output, quic_flow_control* flow);
    quic_encode_result encode_content(std::optional<slice>& content, std::span<std::byte> output);
    bool valid_emission(std::size_t bytes, bool eliciting, bool flight) const;
    quic_information_result store_information(information value, std::span<const std::byte> data);
    void update_information(slice content, information_status status);
    void complete_information(information& value);
    void release_storage(information& value);
    std::optional<slice> select_packet_information(quic_pn_space space, bool probe, const quic_flow_control* flow, bool ack, std::span<std::byte> output);
    bool constrain_slice(slice& content, const information& value, const quic_flow_control* flow) const;
    std::optional<slice> eligible_slice(const information& value, bool probe, const quic_flow_control* flow) const;
    std::optional<slice> select_information(quic_pn_space space, bool probe, const quic_flow_control* flow, std::span<std::byte> output);
    quic_frame information_frame(const information& value, slice content) const;
    quic_encode_result encode_information(slice& content, std::span<std::byte> output);
    void retire_acknowledged_receive(space_state& s, const packet& p);
    void retire_received(space_state& s, std::uint64_t watermark);
    bool can_collect(const packet& p) const;
    struct stream_schedule {
        std::uint64_t stream;
        std::size_t deficit = 0;
    };
    std::vector<stream_schedule> streams;
    std::optional<std::uint64_t> next_stream;
    unsigned consecutive_controls = 0;
    // Bounded outcome summaries contain no flight bytes or retransmission state.
    enum class outcome { pending, neutral, acknowledged, lost, discarded };
    struct congestion_run {
        std::uint64_t first, last, first_number, last_number;
        quic_pn_space space;
        time_point begin, end;
        outcome status;
        bool sampled;
    };
    std::vector<congestion_run> congestion_runs;
    void note_congestion_packet(const packet& p, quic_pn_space space);
    void acknowledge_congestion_runs(quic_pn_space space, std::span<const quic_ack_range> ranges);
    void note_congestion_outcome(const packet& p, outcome status);
    void compact_congestion_runs();
    bool persistent_congestion() const;
    std::optional<std::uint64_t> persistent_through;
    void sync_mtu(time_point now);
    void failed_mtu_opportunity(quic_pn_space space, time_point now);
    std::size_t expire_mtu(time_point now);
    quic_path_mtu mtu;
    quic_congestion congestion;
    quic_pacing pacing;
    std::optional<time_point> send_due;
    std::array<unsigned, 3> probe_grants{};
    quic_send_permission permission(time_point now, std::size_t bytes, bool flight, bool critical, bool probe, quic_pn_space space) const;
    std::optional<slice> select_stream(bool probe, const quic_flow_control* flow) const;
    void commit_schedule();
    void commit_stream_turn();
    bool prefer_data(quic_pn_space space, slice content, std::span<std::byte> output);
    bool data_window_available(std::size_t bytes) const;
    std::optional<slice> stream_slice(const stream_schedule& entry, bool probe, const quic_flow_control* flow) const;
    std::size_t scheduled_overhead = 0;
    std::optional<time_point> scheduled_now;
    quic_recovery_code charge_flow();
    quic_recovery_code check_flow() const;
    quic_send_permission window_permission(std::size_t bytes, bool critical) const;
    void record_emission(time_point now, std::size_t bytes, bool eliciting, bool flight, std::uint64_t generation);
    void finish_loss(std::optional<time_point> newest, time_point now);
    void register_stream(std::uint64_t stream);
    std::uint64_t path_epoch = 0;
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
    recovery_duration base_pto_duration(quic_pn_space space) const;
    void repeat_receipt(space_state& s, quic_receipt receipt, bool eliciting, time_point now);
    void insert_received(space_state& s, std::uint64_t number);
    void note_ack_work(space_state& s, quic_pn_space space, bool gap, bool reordered, time_point now);
    void schedule_ack(space_state& s, quic_pn_space space, bool gap, time_point now);
    bool retire_packet_number(space_state& s, std::uint64_t number);
    void collect_packets(space_state& s);
    quic_recovery_code send_admission(quic_pn_space space) const;
    quic_recovery_code commit_admission(std::uint64_t token, time_point now, std::size_t bytes, bool eliciting, bool flight) const;
    bool sample_ack(const space_state& s, std::span<const quic_ack_range> ranges, const quic_ack_frame& ack, time_point now, quic_pn_space space);
    quic_recovery_events acknowledge_packets(space_state& s, std::span<const quic_ack_range> ranges, quic_pn_space space, time_point now);
    std::optional<quic_recovery_timer> space_probe_timer(quic_pn_space space) const;
    std::optional<quic_recovery_timer> idle_probe_timer() const;
    void update_rtt(std::chrono::steady_clock::duration sample, std::uint64_t delay, quic_pn_space space);
    std::size_t detect_loss(space_state& s, time_point now);
    implementation(quic_recovery_config config, server::resource_budget budget, server::resource_budget critical_budget);
    space_state* state(quic_pn_space s);
    const space_state* state(quic_pn_space s) const;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_RECOVERY_INTERNAL_HPP_
