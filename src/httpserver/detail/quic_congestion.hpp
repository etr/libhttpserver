/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_congestion.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_CONGESTION_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_CONGESTION_HPP_
#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
namespace httpserver::detail {
struct quic_congestion_snapshot {
    std::size_t window, minimum_window, slow_start_threshold;
    std::optional<std::chrono::steady_clock::time_point> recovery_started;
};
// Byte accounting includes packet protection overhead; recovery owns the sent ledger.
class quic_congestion final {
 public:
    using time_point = std::chrono::steady_clock::time_point;
    explicit quic_congestion(std::size_t max_datagram_size = 1200);
    void acknowledge(std::size_t bytes, time_point sent, bool limited);
    void lose(time_point newest_sent, time_point now, bool persistent);
    quic_congestion_snapshot snapshot() const { return {window_, 2 * datagram_, threshold_, recovery_}; }
 private:
    std::size_t datagram_, window_, threshold_ = std::numeric_limits<std::size_t>::max(), credit_ = 0;
    std::optional<time_point> recovery_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_CONGESTION_HPP_
