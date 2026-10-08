/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <httpserver/detail/quic_congestion.hpp>
namespace httpserver::detail {
namespace {
std::size_t saturating_add(std::size_t a, std::size_t b) {
    return a + std::min(b, std::numeric_limits<std::size_t>::max() - a);
}
}  // namespace
quic_congestion::quic_congestion(std::size_t datagram) : datagram_(datagram), window_(0) {
    if (datagram < 1200 || datagram > 65535) throw std::invalid_argument("Invalid QUIC datagram size");
    window_ = std::min(10 * datagram, std::max(std::size_t{14720}, 2 * datagram));
}
void quic_congestion::acknowledge(std::size_t bytes, time_point sent, bool limited) {
    if (limited || (recovery_ && sent <= *recovery_)) return;
    if (window_ < threshold_) {
        window_ = saturating_add(window_, bytes);
        return;
    }
    // Accumulate fractional additive increase without multiplying untrusted byte counts.
    auto rounds = bytes / window_;
    auto remainder = bytes % window_;
    if (remainder >= window_ - credit_) {
        ++rounds;
        credit_ = remainder - (window_ - credit_);
    }
    else credit_ += remainder;
    auto maximum = std::numeric_limits<std::size_t>::max();
    auto growth = rounds > maximum / datagram_ ? maximum : rounds * datagram_;
    window_ = saturating_add(window_, growth);
}
void quic_congestion::lose(time_point newest, time_point now, bool persistent) {
    if (!recovery_ || newest > *recovery_) {
        recovery_ = now;
        window_ = std::max(window_ / 2, 2 * datagram_);
        threshold_ = window_;
        credit_ = 0;
    }
    if (persistent) {
        window_ = 2 * datagram_;
        credit_ = 0;
    }
}
}  // namespace httpserver::detail
