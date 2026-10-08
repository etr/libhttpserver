/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <cmath>
#include <httpserver/detail/quic_pacing.hpp>
namespace httpserver::detail {
namespace {
long double ticks(quic_pacing::duration rtt) {
    return std::max(rtt, quic_pacing::duration(std::chrono::milliseconds(1))).count();
}
}  // namespace
long double quic_pacing::allowance(time_point now, std::size_t window, duration rtt) const {
    if (!charged_ || now <= *charged_) return std::max(0.0L, burst_ - debt_);
    const auto elapsed = static_cast<long double>(now.time_since_epoch().count()) - charged_->time_since_epoch().count();
    return std::min(static_cast<long double>(burst_), std::max(0.0L, burst_ - debt_ + elapsed * window / ticks(rtt)));
}
std::optional<quic_pacing::time_point> quic_pacing::deadline(time_point now, std::size_t bytes, std::size_t window, duration rtt) const {
    if (charged_ && now < *charged_) return *charged_;
    auto needed = bytes - allowance(now, window, rtt);
    if (needed <= 0) return {};
    if (!window || bytes > burst_) return time_point::max();
    auto delay = std::ceil(needed * ticks(rtt) / window);
    auto remaining = static_cast<long double>(time_point::max().time_since_epoch().count()) - now.time_since_epoch().count();
    if (delay >= remaining || delay >= static_cast<long double>(duration::max().count())) return time_point::max();
    return now + duration(static_cast<duration::rep>(delay));
}
void quic_pacing::emitted(time_point now, std::size_t bytes, std::size_t window, duration rtt) {
    debt_ = std::min(static_cast<long double>(burst_), burst_ - allowance(now, window, rtt) + bytes);
    charged_ = now;
}
}  // namespace httpserver::detail
