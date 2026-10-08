/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_DETAIL_QUIC_FLOW_INTERNAL_HPP_
#define SRC_DETAIL_QUIC_FLOW_INTERNAL_HPP_
#include <algorithm>
#include <array>
#include <vector>
#include <httpserver/detail/quic_flow_control.hpp>
namespace httpserver::detail {
inline std::uint64_t flow_add(std::uint64_t a, std::uint64_t b) { return a + std::min(b, k_quic_max_integer - a); }
struct quic_flow_control::implementation {
    struct credit {
        std::uint64_t desired = 0, emitted = 0, acknowledged = 0;
        explicit credit(std::uint64_t initial = 0) : desired(initial), emitted(initial), acknowledged(initial) {}
    };
    struct record {
        std::uint64_t id = 0, received = 0, sent = 0, released = 0, receive_window = 0, send_limit = 0;
        credit receive_limit;
        std::optional<std::uint64_t> sent_final;
        const quic_stream_state* receive_owner = nullptr;
        bool reset_settled = false, reset_sent = false, retired = false;
    };
    quic_stream_ids ids;
    std::array<std::uint64_t, 4> receive_windows{}, send_windows{};
    std::array<std::uint64_t, 2> send_counts{};
    std::array<credit, 2> receive_counts;
    credit receive_limit;
    std::uint64_t receive_window = 0, send_limit = 0, received = 0, sent = 0, released = 0;
    std::size_t maximum;
    server::reservation storage;
    std::vector<record> records;
    quic_flow_code admission = quic_flow_code::ok;
    implementation(quic_endpoint_role role, const quic_transport_parameters& local,
                   const quic_transport_parameters& peer, std::size_t maximum, server::resource_budget budget);
    record* find(std::uint64_t id);
    const record* find(std::uint64_t id) const;
    record* create(std::uint64_t id);
    quic_flow_code record_admission(std::uint64_t id) const;
    quic_flow_result receive_admission(const quic_stream_state& stream, std::uint64_t id, std::uint64_t end, record*& value);
    quic_flow_result consume(const quic_stream_state& stream, std::uint64_t through);
    credit* control(const quic_flow_frame& frame);
    void release(record& value, std::uint64_t through, bool stream_credit);
};
inline quic_flow_result flow_result(quic_stream_result result) {
    switch (result.code) {
    case quic_stream_code::ok: return {};
    case quic_stream_code::stream_state_error: return {quic_flow_code::stream_state_error};
    case quic_stream_code::final_size_error: return {quic_flow_code::final_size_error};
    case quic_stream_code::frame_encoding_error: return {quic_flow_code::frame_encoding_error};
    case quic_stream_code::protocol_violation: return {quic_flow_code::protocol_violation};
    case quic_stream_code::no_memory: return {quic_flow_code::no_memory};
    default: return {quic_flow_code::capacity};
    }
}
}  // namespace httpserver::detail
#endif  // SRC_DETAIL_QUIC_FLOW_INTERNAL_HPP_
