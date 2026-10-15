/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <map>
#include <memory>
#include <httpserver/detail/protocol_limits.hpp>
#include <httpserver/detail/capacity_math.hpp>
#include <httpserver/detail/quic_connection_identity.hpp>
namespace httpserver::detail {
std::size_t quic_transport_descriptor_storage(std::size_t records) {
    const auto record = sizeof(quic_stream_state) + sizeof(std::pair<const std::uint64_t, std::unique_ptr<quic_stream_state>>) + 128;
    return capacity_multiply(records, record);
}
quic_recovery_config native_recovery_limits(std::size_t data_bytes, std::size_t critical_bytes) {
    quic_recovery_config value;
    value.max_datagram_size = 1200;
    value.max_information = value.max_sent_packets = 64;
    value.max_retained_bytes = std::min<std::size_t>(16777216, capacity_add(data_bytes / 4, critical_bytes / 8));
    value.critical_information = value.critical_sent_packets = 16;
    value.critical_retained_bytes = std::min(critical_bytes / 8, value.max_retained_bytes);
    return value;
}
http3_storage_requirements http3_required_storage(const http3_request_limits& limits, std::size_t records) {
    if (records < 7 || records > 65536) throw std::invalid_argument("quic.max_stream_records requires six critical roles and a request, at most 65536");
    http3_storage_requirements need;
    const auto& h = limits.framing;
    // Persistent terminal descriptors remain after semantic exchanges retire.
    need.data_bytes = quic_flow_control::storage_capacity(records);
    need.data_bytes = capacity_add(need.data_bytes, capacity_multiply(records - 6, http3_request_engine::record_storage(limits, false)));
    need.data_bytes = capacity_add(need.data_bytes, h.qpack_retained_bytes);
    need.data_bytes = capacity_add(need.data_bytes, http3_connection::decoded_storage(h.headers));
    need.data_bytes = capacity_add(need.data_bytes, http3_request_engine::semantic_storage(limits, h.headers.max_expanded_bytes, h.headers.max_fields));
    need.data_bytes = capacity_add(need.data_bytes, capacity_multiply(2, h.headers.max_compressed_bytes));
    // One maximum receive window with replacement, plus the bounded ciphertext queue.
    need.data_bytes = capacity_add(need.data_bytes, quic_reassembly::storage_capacity({}));
    need.data_bytes = capacity_add(need.data_bytes, 32768 + 16 * (sizeof(std::shared_ptr<const io_datagram>) + sizeof(server::reservation)));
    need.critical_bytes = capacity_add(quic_transport_descriptor_storage(records), capacity_multiply(records, http3_connection::descriptor_storage()));
    need.critical_bytes = capacity_add(need.critical_bytes, capacity_multiply(3, http3_request_engine::record_storage(limits, true)));
    need.critical_bytes = capacity_add(need.critical_bytes, capacity_multiply(3, http3_request_engine::record_storage(limits, false)));
    need.critical_bytes = capacity_add(need.critical_bytes, sizeof(http3_peer_settings) + 512);
    need.critical_bytes = capacity_add(need.critical_bytes, capacity_multiply(2, h.qpack_output_bytes));
    need.critical_bytes = capacity_add(need.critical_bytes, capacity_add(capacity_multiply(5, h.qpack_instruction_bytes), 10));
    need.critical_bytes = capacity_add(need.critical_bytes, h.settings_bytes);
    need.critical_bytes = capacity_add(need.critical_bytes, http3_connection::output_section_storage(h.headers.max_fields));
    const auto recovery = native_recovery_limits(393216, 393216);
    need.critical_bytes = capacity_add(need.critical_bytes, quic_recovery::storage_capacity(recovery));
    // Native TLS output (three levels), receive lease, parameters, and one CRYPTO
    // receive window. The provider itself is outside the byte-budget contract.
    need.critical_bytes = capacity_add(need.critical_bytes, 3 * 16384 + 16384 + 4096 + 4096);
    need.critical_bytes = capacity_add(need.critical_bytes, quic_reassembly::storage_capacity({}));
    need.critical_bytes = capacity_add(need.critical_bytes, quic_connection_identity::storage_capacity());
    // Recovery retains two charged copies of its payload. Solve the native
    // fractions against the remaining subdivision rather than double charging
    // the root's prepaid envelope.
    need.data_bytes = capacity_multiply(need.data_bytes, 2);
    need.critical_bytes = capacity_add(need.critical_bytes, capacity_add(need.critical_bytes, 2) / 3);
    return need;
}
}  // namespace httpserver::detail
