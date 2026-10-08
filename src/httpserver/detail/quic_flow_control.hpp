/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_flow_control.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_FLOW_CONTROL_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_FLOW_CONTROL_HPP_
#include <memory>
#include <httpserver/detail/quic_stream_state.hpp>
#include <httpserver/detail/quic_transport_parameters.hpp>
namespace httpserver::detail {
enum class quic_flow_code {
    ok, blocked, flow_control_error, stream_limit_error, stream_state_error,
    frame_encoding_error, capacity, no_memory, invalid_consumption,
    final_size_error, protocol_violation
};
struct quic_flow_result {
    quic_flow_code code = quic_flow_code::ok;
    explicit operator bool() const { return code == quic_flow_code::ok; }
};
struct quic_flow_open_result : quic_flow_result { std::uint64_t id = 0; };
struct quic_flow_allowance : quic_flow_result { std::size_t bytes = 0; };
// Serialized connection ledger. Raw stream extraction never grants credit.
// Records include retired IDs and are never evicted or implicitly recreated.
class quic_flow_control final {
 public:
    quic_flow_control(quic_endpoint_role role, const quic_transport_parameters& local,
                      const quic_transport_parameters& peer, std::size_t max_records, server::resource_budget budget);
    quic_flow_control(quic_endpoint_role role, const quic_transport_parameters& local,
                      const quic_transport_parameters& peer, std::size_t max_records, quic_storage_lease storage);
    ~quic_flow_control();
    quic_flow_control(const quic_flow_control&) = delete;
    quic_flow_control& operator=(const quic_flow_control&) = delete;
    quic_flow_open_result open_local(bool unidirectional);
    quic_flow_result observe_peer(std::uint64_t id);
    const quic_stream_ids& ids() const;
    quic_flow_result apply(const quic_flow_frame& frame);
    quic_flow_result receive(quic_stream_state& stream, const quic_stream_frame& frame);
    quic_flow_result receive(quic_stream_state& stream, const quic_reset_stream_frame& frame);
    quic_flow_allowance send_allowance(std::uint64_t id, std::uint64_t offset, std::size_t requested) const;
    quic_flow_result record_stream_sent(std::uint64_t id, std::uint64_t offset, std::size_t length, bool fin);
    quic_flow_result check_reset_sent(std::uint64_t id, std::uint64_t final_size) const;
    quic_flow_result record_reset_sent(std::uint64_t id, std::uint64_t final_size);
    // Ordered receipts exclude all earlier unconsumed body bytes. Duplicate
    // receipts are idempotent; backward or unextracted offsets are rejected.
    quic_flow_result consume_body(const quic_stream_state& stream, std::uint64_t through);
    quic_flow_result consume_protocol(const quic_stream_state& stream, std::uint64_t through);
    quic_flow_result settle_reset(const quic_stream_state& stream);
    quic_flow_result retire(const quic_stream_state& stream);
    std::optional<quic_flow_frame> pending_credit() const;
    std::optional<quic_flow_frame> pending_credit(quic_flow_kind kind, std::uint64_t id = 0) const;
    void credit_emitted(const quic_flow_frame& frame);
    void credit_acknowledged(const quic_flow_frame& frame);
    std::uint64_t received() const;
    std::uint64_t sent() const;
    static std::size_t storage_capacity(std::size_t records);

 private:
    quic_flow_result apply_stream_control(const quic_flow_frame& frame);
    quic_storage_lease storage_owner_;
    struct implementation;
    std::unique_ptr<implementation> impl_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_FLOW_CONTROL_HPP_
