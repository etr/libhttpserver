/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_kqueue_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_KQUEUE_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_KQUEUE_BACKEND_HPP_
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/event.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_managed_socket_backend.hpp>

namespace httpserver {
namespace detail {

class io_kqueue_backend final : public io_managed_socket_backend {
 public:
    io_kqueue_backend();
    ~io_kqueue_backend() override;
    std::uint64_t event_count() const;
    std::uint64_t buffered_eof_events() const;
    std::uint64_t successful_receipts() const;
    std::uint64_t failed_receipts() const;

 private:
    void rearm_locked(registration& record, std::uint32_t mask, std::vector<struct kevent>& changes);
    void apply_changes_locked(const std::vector<struct kevent>& changes);
    void process_event(const struct kevent& event);
    static bool valid_event(const registration& record, const struct kevent& event);
    static bool valid_receipt(const struct kevent& change, const struct kevent& receipt);
    static bool buffered_read_eof(const struct kevent& event);
    std::optional<std::chrono::steady_clock::time_point> reconcile_locked() override;
    void dispatch_event(std::uint64_t token, std::uint32_t events);
    void run_loop() override;
    static std::atomic<std::uint64_t> all_events_;
    static std::atomic<std::uint64_t> all_waits_;
    std::uint64_t events_ = 0;
    std::uint64_t buffered_eof_ = 0;
    std::uint64_t receipts_ = 0;
    std::uint64_t failed_receipts_ = 0;
    using kevent_call = int (*)(int, const struct kevent*, int, struct kevent*, int, const struct timespec*);
    kevent_call kevent_ = &::kevent;
    int kqueue_ = -1;
};

}  // namespace detail
}  // namespace httpserver
#endif  // defined(__APPLE__) || defined(__FreeBSD__)
#endif  // SRC_HTTPSERVER_DETAIL_IO_KQUEUE_BACKEND_HPP_
