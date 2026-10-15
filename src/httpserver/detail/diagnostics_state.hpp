/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "diagnostics_state.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_DIAGNOSTICS_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_DIAGNOSTICS_STATE_HPP_
#include <atomic>
#include <utility>
#include <mutex>
#include <httpserver/server/diagnostics.hpp>
namespace httpserver::detail {
void diagnostic_increment(std::atomic<std::uint64_t>& value, std::uint64_t units = 1) noexcept;
}  // namespace httpserver::detail
namespace httpserver::server {
struct diagnostics::state {
    explicit state(callback fn) : consumer(std::move(fn)) {}
    callback consumer;
    std::array<std::atomic<std::uint64_t>, diagnostic_kind_count> totals{};
    std::array<std::atomic<std::uint64_t>, 3> gauges{};
    std::atomic<std::uint64_t> dropped{0}, failures{0};
    std::atomic_flag dispatching = ATOMIC_FLAG_INIT;
    std::mutex mutex;
    std::array<diagnostic_event, queue_capacity> queue{};
    std::size_t head = 0, count = 0;

    void record(diagnostic_event event) noexcept;
};
}  // namespace httpserver::server
#endif  // SRC_HTTPSERVER_DETAIL_DIAGNOSTICS_STATE_HPP_
