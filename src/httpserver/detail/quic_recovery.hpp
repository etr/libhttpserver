/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_recovery.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_RECOVERY_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_RECOVERY_HPP_
#include <chrono>
#include <memory>
#include <httpserver/detail/quic_congestion.hpp>
#include <httpserver/detail/quic_frame.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/detail/quic_storage.hpp>
namespace httpserver::detail {
class quic_flow_control;
enum class quic_pn_space { initial, handshake, application };
enum class quic_recovery_code { ok, invalid, capacity, no_memory, no_space, no_data, discarded, busy, congestion_blocked, pacing_blocked, unavailable };
enum class quic_receipt { fresh, duplicate, retired, invalid };
struct quic_recovery_result {
    quic_recovery_code code = quic_recovery_code::ok;
    explicit operator bool() const { return code == quic_recovery_code::ok; }
};
struct quic_recovery_config {
    std::size_t max_receive_ranges = 64, max_sent_packets = 256, max_information = 64, max_retained_bytes = 65536;
    std::size_t critical_information = 0, critical_retained_bytes = 0, critical_sent_packets = 0;
    std::chrono::microseconds local_max_ack_delay{25000}, peer_max_ack_delay{25000};
    unsigned local_ack_delay_exponent = 3, peer_ack_delay_exponent = 3;
    std::size_t max_datagram_size = 1200, control_reserve = 1200;
    quic_endpoint_role role = quic_endpoint_role::server;
};
struct quic_ack_plan : quic_recovery_result {
    std::size_t bytes = 0;
    std::uint64_t generation = 0;
};
struct quic_send_request {
    std::size_t max_wire_bytes = 1200, protection_overhead = 40;
    bool probe = false, limited = false;
};
struct quic_send_permission : quic_recovery_result {
    std::optional<std::chrono::steady_clock::time_point> deadline{};
};
struct quic_send_plan : quic_recovery_result {
    std::uint64_t token = 0, packet_number = 0;
    std::size_t bytes = 0;
    std::optional<std::chrono::steady_clock::time_point> deadline{};
    bool ack_eliciting = false;
    std::optional<quic_stream_frame> stream{};
    std::optional<quic_reset_stream_frame> reset{};
    std::optional<quic_flow_frame> flow{};
};
struct quic_recovery_events : quic_recovery_result {
    std::size_t acknowledged_bytes = 0, lost_bytes = 0, discarded_bytes = 0, acknowledged_packets = 0;
    std::optional<std::uint64_t> application_generation{};
    std::optional<quic_pn_space> probe_space{};
    unsigned probes = 0;
    std::array<bool, 3> acknowledge_spaces{};
};
struct quic_rtt_state {
    using duration = std::chrono::steady_clock::duration;
    duration latest = std::chrono::milliseconds(333), smoothed = std::chrono::milliseconds(333);
    duration variation = std::chrono::microseconds(166500), minimum{};
    bool sampled = false;
};
struct quic_recovery_environment {
    std::array<bool, 3> write_keys{true, true, true};
    bool handshake_confirmed = false, peer_validated_endpoint = true, send_permitted = true;
};
using quic_information_id = std::uint64_t;
enum class quic_information_kind { crypto, stream, reset_stream, flow };
struct quic_information_result : quic_recovery_result {
    quic_information_id id = 0;
};
struct quic_information_completion {
    quic_information_id id;
    quic_information_kind kind;
    quic_pn_space space;
    std::uint64_t stream = 0, through_offset = 0;
    bool fin = false;
    std::optional<quic_flow_frame> flow{};
};
struct quic_recovery_timer {
    enum class kind { acknowledge, detect_loss, probe };
    kind action;
    quic_pn_space space;
    std::chrono::steady_clock::time_point deadline;
};
// Connection-owner serialized. All input bytes are copied; time is explicit.
class quic_recovery final {
 public:
    using time_point = std::chrono::steady_clock::time_point;
    quic_recovery(quic_recovery_config config, server::resource_budget budget);
    quic_recovery(quic_recovery_config config, quic_storage_lease data, quic_storage_lease critical);
    static std::size_t storage_capacity(quic_recovery_config config);
    ~quic_recovery();
    quic_recovery(const quic_recovery&) = delete;
    quic_recovery& operator=(const quic_recovery&) = delete;
    // Inspect after authentication, before applying frames; commit only accepted packets.
    quic_receipt inspect_received(quic_pn_space space, std::uint64_t number) const;
    quic_recovery_result receive_packet(quic_pn_space space, std::uint64_t number, bool ack_eliciting, time_point now);
    std::optional<time_point> ack_deadline(quic_pn_space space) const;
    quic_ack_plan prepare_ack(quic_pn_space space, std::span<std::byte> output, time_point now) const;
    void publish_ack(quic_pn_space space, std::uint64_t generation);
    // Reserves a record BEFORE encryption. Abandoning burns the packet number.
    // One outstanding preparation per owner; no allocation on successful commit.
    quic_send_plan reserve_packet(quic_pn_space space);
    quic_recovery_result abandon_packet(std::uint64_t token);
    quic_recovery_result commit_sent(std::uint64_t token, time_point now, std::size_t wire_bytes,
                                    bool ack_eliciting, bool in_flight, std::uint64_t key_generation = 0);
    quic_recovery_events receive_ack(quic_pn_space space, const quic_ack_frame& ack, time_point now);
    std::size_t bytes_in_flight() const;
    quic_rtt_state rtt() const;
    void set_environment(quic_recovery_environment environment, time_point now);
    quic_recovery_events expire(time_point now);
    std::optional<quic_recovery_timer> next_deadline() const;
    unsigned pto_count() const;
    quic_information_result retain_crypto(quic_pn_space space, std::uint64_t offset, std::span<const std::byte> data);
    quic_information_result retain_stream(const quic_stream_frame& frame);
    quic_information_result retain_flow(const quic_flow_frame& frame);
    quic_information_result retain_reset(const quic_reset_stream_frame& frame);
    quic_recovery_result cancel_information(quic_information_id id);
    std::optional<std::uint64_t> delivered_prefix(quic_information_id id) const;
    std::optional<quic_information_completion> take_completion();
    // Writes fresh frames, selecting one bounded information slice and current ACK.
    // Caller protects using packet_number, reports actual emission or abandons.
    quic_send_plan prepare_packet(quic_pn_space space, std::span<std::byte> output, time_point now, bool probe = false);
    quic_send_plan prepare_packet(quic_pn_space space, std::span<std::byte> output, time_point now, quic_flow_control& flow, bool probe = false);
    // Check immediately before serialized transport emission; commit records that emission.
    quic_send_plan prepare_scheduled_packet(quic_pn_space space, std::span<std::byte> output, time_point now,
                                           quic_send_request request, quic_flow_control& flow);
    quic_send_permission check_scheduled_emission(std::uint64_t token, time_point now, std::size_t wire_bytes,
                                                 bool ack_eliciting, bool in_flight) const;
    quic_congestion_snapshot congestion() const;
    std::optional<time_point> next_send_deadline() const;
    quic_recovery_events discard_space(quic_pn_space space);

 private:
    quic_send_plan prepare(quic_pn_space space, std::span<std::byte> output, time_point now, quic_flow_control* flow, bool probe, bool ack_only = false);
    quic_send_plan finish_preparation(quic_send_plan plan, quic_pn_space space, std::span<std::byte> output, time_point now);
    quic_send_plan admit_scheduled(quic_send_plan plan, time_point now, quic_send_request request);
    quic_send_plan reserve(quic_pn_space space, bool critical);
    quic_recovery_code empty_plan_code(quic_pn_space space) const;
    void append_ack(quic_send_plan& plan, quic_pn_space space, std::span<std::byte> output, time_point now);
    quic_storage_lease data_owner_, critical_owner_;
    struct implementation;
    std::unique_ptr<implementation> impl_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_RECOVERY_HPP_
