/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_stream_state.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_STREAM_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_STREAM_STATE_HPP_
#include <array>
#include <optional>
#include <httpserver/detail/quic_frame.hpp>
#include <httpserver/detail/quic_reassembly.hpp>
namespace httpserver::detail {
struct quic_stream_identity {
    quic_endpoint_role initiator;
    bool unidirectional;
    std::uint64_t ordinal;
};
std::optional<quic_stream_identity> quic_stream_id(std::uint64_t id);
// Connection-owned high-water counters only. The caller materializes and
// retires streams; it must never recreate a retired ID from these counters.
class quic_stream_ids final {
 public:
    explicit quic_stream_ids(quic_endpoint_role local) : local_(local) {}
    std::optional<std::uint64_t> open_local(bool unidirectional);
    quic_stream_result observe_peer(std::uint64_t id);
    bool opened(std::uint64_t id) const;
    quic_endpoint_role local_role() const { return local_; }
    std::uint64_t opened_count(std::uint8_t stream_class) const;

 private:
    quic_endpoint_role local_;
    std::array<std::uint64_t, 4> counts_{};
};
enum class quic_receive_state { unavailable, receiving, size_known, data_received, data_consumed, reset_received, reset_consumed };
enum class quic_send_state { unavailable, ready, sending, fin_sent, data_acknowledged, reset_pending, reset_sent, reset_acknowledged };
enum class quic_terminal_kind { eof, reset };
struct quic_stream_terminal {
    quic_terminal_kind kind;
    std::uint64_t error = 0;
};
struct quic_stream_read_result {
    quic_stream_code code = quic_stream_code::ok;
    std::size_t bytes = 0;
};
// Serialized by the connection owner. No transport scheduling or callbacks.
// Construction requires an opened ID from a same-role connection counter.
class quic_stream_state final {
 public:
    quic_stream_state(std::uint64_t id, quic_endpoint_role local, const quic_stream_ids& ids,
                      quic_stream_limits limits, server::resource_budget budget);
    quic_stream_state(const quic_stream_state&) = delete;
    quic_stream_state& operator=(const quic_stream_state&) = delete;
    quic_stream_result receive(const quic_stream_frame& frame);
    quic_stream_result receive(const quic_reset_stream_frame& frame);
    quic_stream_result receive(const quic_stop_sending_frame& frame);
    quic_stream_read_result read(std::span<std::byte> output);
    quic_stream_result record_stream_sent(std::uint64_t offset, std::size_t length, bool fin);
    quic_stream_result record_reset_sent(const quic_reset_stream_frame& frame);
    quic_stream_result acknowledge_all_stream_data();
    quic_stream_result acknowledge_reset();
    std::optional<quic_reset_stream_frame> take_reset_request();
    std::optional<quic_stream_terminal> take_terminal();
    quic_receive_state receive_state() const { return receive_; }
    quic_send_state send_state() const { return send_; }
    std::optional<std::uint64_t> final_size() const { return final_; }
    std::uint64_t highest_received() const { return highest_received_; }
    std::uint64_t highest_sent() const { return highest_sent_; }
    std::uint64_t consumed() const { return reassembly_.consumed(); }
    std::size_t buffered_bytes() const { return reassembly_.buffered_bytes(); }
    std::size_t ranges() const { return reassembly_.ranges(); }
    std::size_t retained_storage() const { return reassembly_.retained_storage(); }

 private:
    quic_stream_result check_frame(std::uint64_t id, bool receive_half) const;
    quic_stream_result check_final(std::uint64_t end, bool fixes_size) const;
    void update_receive();
    std::uint64_t id_;
    quic_receive_state receive_ = quic_receive_state::receiving;
    quic_send_state send_ = quic_send_state::ready;
    quic_reassembly reassembly_;
    std::uint64_t highest_received_ = 0, highest_sent_ = 0;
    std::optional<std::uint64_t> final_, sent_final_;
    std::optional<quic_reset_stream_frame> reset_;
    std::optional<quic_stream_terminal> terminal_;
    bool reset_request_pending_ = false, terminal_delivered_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_STREAM_STATE_HPP_
