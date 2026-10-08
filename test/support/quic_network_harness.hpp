/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_SUPPORT_QUIC_NETWORK_HARNESS_HPP_
#define TEST_SUPPORT_QUIC_NETWORK_HARNESS_HPP_
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <httpserver/detail/fake_io_backend.hpp>
#include <httpserver/detail/quic_datagram_dispatch.hpp>
namespace quic_test {
namespace hd = httpserver::detail;
class logical_clock {
 public:
    using time_point = std::chrono::steady_clock::time_point;
    bool advance(std::int64_t nanoseconds);
    std::int64_t ticks() const { return ticks_; }
    time_point now() const { return time_point(std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(ticks_))); }
 private:
    std::int64_t ticks_ = 0;
};
struct harness_limits {
    std::size_t packets = 16, bytes = 16384, packet_bytes = 4096;
    std::size_t timers = 16, actions = 128, trace_bytes = 2097152;
    std::size_t owner_packets = 2, owner_bytes = 8192;
};
struct resource_snapshot {
    std::size_t packets = 0, bytes = 0, timers = 0, owner_pending = 0;
};
struct timer_result {
    std::uint64_t id;
    httpserver::http::outcome_code code;
};
// Test-only dispatch/ownership/timer rig, not a QUIC transport engine. Trace
// and delivered values are bounded observation copies, not pending storage.
class network_harness {
 public:
    explicit network_harness(harness_limits limits = {});
    ~network_harness();
    network_harness(const network_harness&) = delete;
    network_harness& operator=(const network_harness&) = delete;
    bool register_endpoint(unsigned endpoint);
    bool retire_endpoint(unsigned endpoint);
    bool destroy_endpoint(unsigned endpoint);
    std::optional<std::uint64_t> send(const hd::io_datagram& packet);
    std::optional<std::uint64_t> duplicate(std::uint64_t id);
    bool drop(std::uint64_t id);
    std::optional<hd::datagram_dispatch_code> deliver(std::uint64_t id);
    bool advance(std::int64_t nanoseconds);
    bool drain();
    std::optional<std::uint64_t> timer(std::int64_t delay);
    bool cancel(std::uint64_t id);
    void teardown();
    resource_snapshot resources() const;
    std::vector<std::uint64_t> pending_ids() const;
    std::weak_ptr<const hd::io_datagram> packet_lifetime(std::uint64_t id) const;
    const std::vector<hd::io_datagram>& delivered() const;
    const std::vector<timer_result>& completed_timers() const;
    const std::vector<std::uint8_t>& trace() const;
    std::int64_t ticks() const;

 private:
    struct implementation;
    std::unique_ptr<implementation> impl_;
};
hd::io_datagram short_packet(unsigned endpoint, unsigned marker);
}  // namespace quic_test
#endif  // TEST_SUPPORT_QUIC_NETWORK_HARNESS_HPP_
