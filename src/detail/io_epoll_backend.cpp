/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_epoll_backend.hpp>
#if defined(__linux__)
#include <sys/epoll.h>
#include <algorithm>
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/io_timer_sweep.hpp>

namespace httpserver {
namespace detail {
namespace {
void control(int epoll, int command, int fd, epoll_event* event) {
    int result;
    do {
        result = ::epoll_ctl(epoll, command, fd, event);
    } while (result < 0 && errno == EINTR);
    if (result < 0) throw std::runtime_error("httpserver: epoll_ctl failed");
}
}  // namespace

io_epoll_backend::io_epoll_backend() {
    epoll_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_ < 0 || !wake_.valid()) {
        closed_ = true;
        return;
    }
    try {
        epoll_event event{};
        event.events = EPOLLIN | EPOLLET;
        event.data.u64 = 1;
        control(epoll_, EPOLL_CTL_ADD, wake_.read_handle(), &event);
        thread_ = std::thread([this] { run_managed(); });
    } catch (...) {
        ::close(epoll_);
        epoll_ = -1;
        throw;
    }
}

io_epoll_backend::~io_epoll_backend() {
    close();
    if (epoll_ >= 0) ::close(epoll_);
}

void io_epoll_backend::rearm_locked(registration& record, std::uint32_t mask) {
    if (mask == 0) {
        if (record.installed) control(epoll_, EPOLL_CTL_DEL, record.socket, nullptr);
        record.installed = false;
        record.armed = false;
        return;
    }
    if (record.installed && record.armed && record.mask == mask) return;
    epoll_event event{};
    event.events = mask | EPOLLET | EPOLLONESHOT;
    if (!record.listener) event.events |= EPOLLRDHUP;
    event.data.u64 = record.token;
    control(epoll_, record.installed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD,
            record.socket, &event);
    record.mask = mask;
    record.installed = true;
    record.armed = mask;
}

std::optional<std::chrono::steady_clock::time_point> io_epoll_backend::reconcile_locked() {
    std::unordered_map<std::uint64_t, std::uint32_t> masks;
    const auto deadline = scan_interest_locked(masks);
    for (auto& entry : connections_) rearm_locked(*entry.second, masks[entry.first]);
    return deadline;
}

void io_epoll_backend::dispatch_event(std::uint64_t token, std::uint32_t events) {
    const auto errors = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    const auto directions = events & errors ? EPOLLIN | EPOLLOUT : events;
    dispatch_ready(token, directions, EPOLLIN | EPOLLOUT);
}

void io_epoll_backend::run_loop() {
    try {
        epoll_event events[64];
        for (;;) {
            const auto timeout = prepare_wait();
            if (timeout < 0) return;
            const int count = ::epoll_wait(epoll_, events, 64, timeout);
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("httpserver: epoll_wait failed");
            }
            finish_wait();
            for (int i = 0; i < count; ++i) {
                if (events[i].data.u64 != 1) dispatch_event(events[i].data.u64, events[i].events);
            }
            expire_timers();
        }
    } catch (...) {
        shutdown();  // fatal kernel/control failures must resolve pending work
    }
}

}  // namespace detail
}  // namespace httpserver
#endif  // defined(__linux__)
