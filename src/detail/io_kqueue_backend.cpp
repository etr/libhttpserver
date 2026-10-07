/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_kqueue_backend.hpp>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/event.h>
#include <fcntl.h>
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
constexpr std::uint32_t k_read = 1;
constexpr std::uint32_t k_write = 4;
void* event_token(std::uint64_t token) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
}
std::uint64_t event_identity(const struct kevent& event) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(event.udata));
}
}  // namespace

std::atomic<std::uint64_t> io_kqueue_backend::all_events_{0};
std::atomic<std::uint64_t> io_kqueue_backend::all_waits_{0};

io_kqueue_backend::io_kqueue_backend() {
    kqueue_ = ::kqueue();
    if (kqueue_ < 0 || !wake_.valid()) {
        closed_ = true;
        return;
    }
    try {
        if (::fcntl(kqueue_, F_SETFD, FD_CLOEXEC) < 0) {
            throw std::runtime_error("httpserver: kqueue close-on-exec failed");
        }
        struct kevent change;
        EV_SET(&change, wake_.read_handle(), EVFILT_READ, EV_ADD | EV_CLEAR | EV_RECEIPT, 0, 0, event_token(1));
        struct kevent receipt;
        const struct timespec immediate{};
        if (kevent_(kqueue_, &change, 1, &receipt, 1, &immediate) != 1
            || !(receipt.flags & EV_ERROR) || receipt.data != 0) {
            throw std::runtime_error("httpserver: kqueue wake registration failed");
        }
        ++receipts_;
        thread_ = std::thread([this] { run_managed(); });
    } catch (...) {
        ::close(kqueue_);
        kqueue_ = -1;
        throw;
    }
}

io_kqueue_backend::~io_kqueue_backend() {
    close();
    if (kqueue_ >= 0) ::close(kqueue_);
}

void io_kqueue_backend::rearm_locked(registration& record, std::uint32_t mask,
                                     std::vector<struct kevent>& changes) {
    for (const auto direction : {k_read, k_write}) {
        if ((mask & direction) && (record.armed & direction)) continue;
        if (!(mask & direction) && !(record.armed & direction)) continue;
        struct kevent change;
        const auto filter = direction == k_read ? EVFILT_READ : EVFILT_WRITE;
        // Re-enabling a dispatch filter reevaluates latent readiness. EV_CLEAR
        // would instead require a new network transition after a bounded read.
        const auto flags = (mask & direction)
            ? ((record.installed & direction) ? EV_ENABLE : EV_ADD | EV_DISPATCH)
            : EV_DISABLE;
        EV_SET(&change, record.socket, filter, flags | EV_RECEIPT, 0, 0, event_token(record.token));
        changes.push_back(change);
    }
}

void io_kqueue_backend::apply_changes_locked(const std::vector<struct kevent>& changes) {
    if (changes.empty()) return;
    std::vector<struct kevent> receipts(changes.size());
    const struct timespec immediate{};
    // Separate receipt phase: readiness cannot displace a control receipt.
    // An interrupted control batch is ambiguous; fail closed instead of replay.
    const auto count = kevent_(kqueue_, changes.data(), static_cast<int>(changes.size()),
                              receipts.data(), static_cast<int>(receipts.size()), &immediate);
    if (count != static_cast<int>(changes.size())) {
        throw std::runtime_error("httpserver: kqueue control receipts unavailable");
    }
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const auto& change = changes[i];
        const auto& receipt = receipts[i];
        if ((receipt.flags & EV_ERROR) && receipt.data != 0) ++failed_receipts_;
        if (!valid_receipt(change, receipt)) {
            throw std::runtime_error("httpserver: kqueue registration failed");
        }
        ++receipts_;
        const auto found = tokens_.find(event_identity(change));
        if (found == tokens_.end()) continue;
        auto& record = *connections_.at(found->second);
        const auto direction = change.filter == EVFILT_READ ? k_read : k_write;
        record.installed |= direction;
        if (change.flags & EV_DISABLE) record.armed &= ~direction;
        else record.armed |= direction;
    }
}

std::optional<std::chrono::steady_clock::time_point> io_kqueue_backend::reconcile_locked() {
    std::unordered_map<std::uint64_t, std::uint32_t> masks;
    const auto deadline = scan_interest_locked(masks);
    std::vector<struct kevent> changes;
    for (auto& entry : connections_) rearm_locked(*entry.second, masks[entry.first], changes);
    apply_changes_locked(changes);
    return deadline;
}

void io_kqueue_backend::dispatch_event(std::uint64_t token, std::uint32_t events) {
    dispatch_ready(token, events, events);
}

bool io_kqueue_backend::valid_receipt(const struct kevent& change, const struct kevent& receipt) {
    return (receipt.flags & EV_ERROR) && receipt.data == 0
        && receipt.ident == change.ident && receipt.filter == change.filter && receipt.udata == change.udata;
}

bool io_kqueue_backend::valid_event(const registration& record, const struct kevent& event) {
    const auto direction = event.filter == EVFILT_READ ? k_read : k_write;
    return event.ident == static_cast<std::uintptr_t>(record.socket)
        && (event.filter == EVFILT_READ || event.filter == EVFILT_WRITE) && (record.installed & direction);
}

bool io_kqueue_backend::buffered_read_eof(const struct kevent& event) {
    return event.filter == EVFILT_READ && (event.flags & EV_EOF) && event.data > 0;
}

void io_kqueue_backend::process_event(const struct kevent& event) {
    const auto token = event_identity(event);
    if (token == 1) return;
    const auto direction = event.filter == EVFILT_READ ? k_read : k_write;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto found = tokens_.find(token);
        if (closed_ || found == tokens_.end()) return;
        const auto& record = *connections_.at(found->second);
        if (!valid_event(record, event)) return;
        ++events_;
        ++all_events_;
        if (buffered_read_eof(event)) ++buffered_eof_;
        if (event.flags & EV_ERROR) throw std::runtime_error("httpserver: unexpected kqueue event error");
    }
    // EV_EOF/fflags/data are hints. Only recv/send decides the terminal
    // result, so write EOF or a socket error cannot discard buffered reads.
    dispatch_event(token, direction);
}

std::uint64_t io_kqueue_backend::event_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return events_;
}
std::uint64_t io_kqueue_backend::buffered_eof_events() const {
    std::lock_guard<std::mutex> lock(mu_);
    return buffered_eof_;
}
std::uint64_t io_kqueue_backend::successful_receipts() const {
    std::lock_guard<std::mutex> lock(mu_);
    return receipts_;
}

std::uint64_t io_kqueue_backend::failed_receipts() const {
    std::lock_guard<std::mutex> lock(mu_);
    return failed_receipts_;
}

void io_kqueue_backend::run_loop() {
    try {
        struct kevent events[64];
        for (;;) {
            const auto timeout = prepare_wait();
            if (timeout < 0) return;
            const struct timespec remaining{timeout / 1000, (timeout % 1000) * 1000000L};
            const int count = kevent_(kqueue_, nullptr, 0, events, 64, &remaining);
            if (count < 0) {
                if (errno == EINTR) continue;  // recompute the monotonic remaining deadline
                throw std::runtime_error("httpserver: kevent wait failed");
            }
            finish_wait();
            ++all_waits_;
            // Read directions first; neither EOF nor write errors bypass recv.
            for (const auto filter : {EVFILT_READ, EVFILT_WRITE}) {
                for (int i = 0; i < count; ++i) {
                    if (events[i].filter == filter) process_event(events[i]);
                }
            }
            expire_timers();
        }
    } catch (...) {
        shutdown();  // fatal kernel/control failures must resolve pending work
    }
}

}  // namespace detail
}  // namespace httpserver
#endif  // defined(__APPLE__) || defined(__FreeBSD__)
