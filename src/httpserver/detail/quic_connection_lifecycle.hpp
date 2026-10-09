/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_connection_lifecycle.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_CONNECTION_LIFECYCLE_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_CONNECTION_LIFECYCLE_HPP_
#include <chrono>
#include <httpserver/detail/quic_frame.hpp>
namespace httpserver::detail {
enum class quic_lifecycle_state { active, closing, draining, closed };
// Owner serialized. No clock reads, key ownership, I/O or callbacks.
class quic_connection_lifecycle final {
 public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;
    static constexpr std::size_t max_reason = 256;
    static constexpr unsigned max_close_responses = 3;
    quic_connection_lifecycle(std::chrono::milliseconds local, std::chrono::milliseconds peer, time_point now);
    void negotiate(std::chrono::milliseconds local, std::chrono::milliseconds peer);
    void received(time_point now);
    void emitted(bool possibly_sent, bool ack_eliciting, time_point now);
    bool local_close(const quic_close_frame& frame, time_point now, duration pto);
    bool peer_close(const quic_close_frame& frame, time_point now, duration pto);
    bool stateless_reset(time_point now, duration pto);
    bool expire(time_point now, duration pto);
    quic_lifecycle_state state() const { return state_; }
    std::optional<time_point> deadline(duration pto) const;
    quic_close_frame close_frame(bool application_keys) const;
    void closing_packet(time_point now);
    std::optional<time_point> response_deadline() const;
    bool response_due(time_point now) const;
    void close_emitted(bool possibly_sent, time_point now, duration pto);

 private:
    bool terminate(quic_lifecycle_state state, const quic_close_frame& frame, time_point now, duration pto);
    quic_lifecycle_state state_ = quic_lifecycle_state::active;
    duration idle_{};
    time_point activity_{}, response_after_{};
    std::optional<time_point> endpoint_;
    std::array<std::byte, max_reason> reason_{};
    std::size_t reason_size_ = 0;
    std::uint64_t error_ = 0, frame_type_ = 0;
    unsigned responses_ = 0;
    bool application_ = false, first_send_ = true, respond_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_CONNECTION_LIFECYCLE_HPP_
