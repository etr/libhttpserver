/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <stdexcept>
#include <httpserver/detail/quic_path_mtu.hpp>
#include <httpserver/detail/quic_codec.hpp>
namespace httpserver::detail {
quic_path_mtu::quic_path_mtu(std::size_t local) : local_(local) {
    if (local < 1200 || local > 65535) throw std::invalid_argument("Invalid QUIC datagram size");
}
std::size_t quic_path_mtu::ceiling() const { return std::min(local_, peer_); }
bool quic_path_mtu::set_peer(std::uint64_t peer) {
    if (peer < 1200 || peer > k_quic_max_integer) return false;
    peer_ = static_cast<std::size_t>(std::min(peer, std::uint64_t{65535}));
    reset();
    return true;
}
void quic_path_mtu::reset() {
    confirmed_ = 1200;
    probe_id_ = probe_size_ = 0;
    attempts_ = failures_ = 0;
    probe_deadline_.reset();
    cooldown_.reset();
}
std::size_t quic_path_mtu::candidate(time_point now) const {
    if (probe_deadline_ || (cooldown_ && now < *cooldown_) || confirmed_ == ceiling()) return 0;
    return std::min(confirmed_ + 128, ceiling());
}
void quic_path_mtu::sent(std::uint64_t id, std::size_t bytes, time_point deadline) {
    probe_id_ = id;
    probe_size_ = bytes;
    probe_deadline_ = deadline;
    if (cooldown_ && deadline > *cooldown_) {
        attempts_ = 0;
        cooldown_.reset();
    }
    ++attempts_;
}
bool quic_path_mtu::acknowledge(std::uint64_t id, time_point now) {
    if (!probe_deadline_ || id != probe_id_ || now >= *probe_deadline_) return false;
    confirmed_ = probe_size_;
    probe_deadline_.reset();
    attempts_ = failures_ = 0;
    return true;
}
std::optional<std::uint64_t> quic_path_mtu::expire(time_point now, time_point cooldown_until) {
    if (!probe_deadline_ || now < *probe_deadline_) return {};
    const auto id = probe_id_;
    probe_deadline_.reset();
    if (attempts_ >= 3) cooldown_ = cooldown_until;
    return id;
}
bool quic_path_mtu::failed_datagram(time_point cooldown_until) {
    if (confirmed_ == 1200 || ++failures_ < 3) return false;
    confirmed_ = 1200;
    probe_deadline_.reset();
    cooldown_ = cooldown_until;
    attempts_ = failures_ = 0;
    return true;
}
}  // namespace httpserver::detail
