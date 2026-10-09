/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_path_mtu.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_PATH_MTU_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_PATH_MTU_HPP_
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <optional>
namespace httpserver::detail {
// Sizes are UDP payload bytes, including all packet protection overhead.
class quic_path_mtu final {
 public:
    explicit quic_path_mtu(std::size_t local_ceiling);
    using time_point = std::chrono::steady_clock::time_point;
    bool set_peer(std::uint64_t ceiling);
    void reset();
    std::size_t candidate(time_point now) const;
    void sent(std::uint64_t id, std::size_t bytes, time_point deadline);
    bool acknowledge(std::uint64_t id, time_point now);
    std::optional<std::uint64_t> expire(time_point now, time_point cooldown_until);
    bool failed_datagram(time_point cooldown_until);
    void progress() { failures_ = 0; }
    std::optional<time_point> deadline() const { return probe_deadline_; }
    std::size_t ceiling() const;
    std::size_t confirmed() const { return confirmed_; }
 private:
    std::size_t local_, peer_ = 65527, confirmed_ = 1200, probe_size_ = 0;
    std::uint64_t probe_id_ = 0;
    unsigned attempts_ = 0, failures_ = 0;
    std::optional<time_point> probe_deadline_, cooldown_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_PATH_MTU_HPP_
