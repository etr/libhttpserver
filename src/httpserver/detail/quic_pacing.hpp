/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_pacing.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_PACING_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_PACING_HPP_
#include <chrono>
#include <cstddef>
#include <optional>
namespace httpserver::detail {
// One window per RTT, at most two datagrams of accumulated idle allowance.
class quic_pacing final {
 public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;
    explicit quic_pacing(std::size_t datagram = 1200) : burst_(2 * datagram) {}
    std::optional<time_point> deadline(time_point now, std::size_t bytes, std::size_t window, duration rtt) const;
    void emitted(time_point now, std::size_t bytes, std::size_t window, duration rtt);
 private:
    long double allowance(time_point now, std::size_t window, duration rtt) const;
    std::size_t burst_;
    long double debt_ = 0;
    std::optional<time_point> charged_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_PACING_HPP_
