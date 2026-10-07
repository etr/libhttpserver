/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_epoll_backend.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_EPOLL_BACKEND_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_EPOLL_BACKEND_HPP_
#if defined(__linux__)
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

class io_epoll_backend final : public io_managed_socket_backend {
 public:
    io_epoll_backend();
    ~io_epoll_backend() override;

 private:
    void rearm_locked(registration& record, std::uint32_t mask);
    std::optional<std::chrono::steady_clock::time_point> reconcile_locked() override;
    void dispatch_event(std::uint64_t token, std::uint32_t events);
    void run_loop() override;
    int epoll_ = -1;
};

}  // namespace detail
}  // namespace httpserver
#endif  // defined(__linux__)
#endif  // SRC_HTTPSERVER_DETAIL_IO_EPOLL_BACKEND_HPP_
