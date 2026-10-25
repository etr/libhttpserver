/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#ifndef EXAMPLES_HOST_POLL_HPP_
#define EXAMPLES_HOST_POLL_HPP_

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <poll.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cinttypes>
#include <optional>
#include <stdexcept>
#include <vector>

#include <httpserver/server/readiness.hpp>

namespace v3_host {
namespace srv = httpserver::server;
using clock_type = std::chrono::steady_clock;
#if defined(_WIN32)
using slot = WSAPOLLFD;
constexpr std::int16_t readable = POLLRDNORM;
constexpr std::int16_t writable = POLLWRNORM;
#else
using slot = pollfd;
constexpr std::int16_t readable = POLLIN;
constexpr std::int16_t writable = POLLOUT;
#endif

// Diagnostic branch only. Stop/error rows are emitted immediately; dispatch
// history is bounded in memory. Clocks, sequence writes and I/O perturb timing.
struct capture_row {
    std::uint64_t sequence = 0;
    std::int64_t ticks = 0;
    char detail[256]{};
};
inline std::atomic<std::uint64_t> capture_sequence{0};
inline std::atomic<unsigned> capture_count{0};
inline std::array<capture_row, 64> capture_rows{};
inline std::array<capture_row, 16> capture_dispatch_rows{};
inline bool capture_wait_reported = false;  // Written only by the host thread.
inline unsigned capture_dispatch_count = 0;  // Written only by the host thread.

inline capture_row capture_stamp() noexcept {
    capture_row row;
    row.sequence = capture_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
#if defined(_WIN32)
    LARGE_INTEGER ticks{};
    ::QueryPerformanceCounter(&ticks);
    row.ticks = ticks.QuadPart;
#endif
    return row;
}

inline void capture_emit_early(const capture_row& row) noexcept {
    std::fprintf(stderr, "WAIT_CAPTURE_EARLY seq=%" PRIu64 " ticks=%" PRId64 " %s\n",
        row.sequence, row.ticks, row.detail);
    std::fflush(stderr);
}

inline void capture_event(const char* event) noexcept {
    const auto index = capture_count.fetch_add(1, std::memory_order_relaxed);
    if (index >= capture_rows.size()) return;
    auto row = capture_stamp();
    std::snprintf(row.detail, sizeof(row.detail), "%s", event);
    capture_rows[index] = row;
    capture_emit_early(row);
}

inline void capture_dispatch(const char* event) noexcept {
    auto row = capture_stamp();
    std::snprintf(row.detail, sizeof(row.detail), "%s", event);
    capture_dispatch_rows[capture_dispatch_count++ % capture_dispatch_rows.size()] = row;
}

#if defined(_WIN32)
inline void capture_wait_failure(int ready, int error, const std::vector<slot>& fds,
                                 const std::vector<srv::socket_interest>& interests) noexcept {
    if (!capture_wait_reported) {
        capture_wait_reported = true;
        for (const auto& row : capture_dispatch_rows) if (row.sequence) capture_emit_early(row);
    }
    capture_event("wait_failure");
    const auto index = capture_count.fetch_add(1, std::memory_order_relaxed);
    if (index < capture_rows.size()) {
        auto row = capture_stamp();
        std::snprintf(row.detail, sizeof(row.detail), "wait_result rc=%d error=%d fd_count=%" PRIu64,
            ready, error, static_cast<std::uint64_t>(fds.size()));
        capture_rows[index] = row;
        capture_emit_early(row);
    }
    for (std::size_t i = 0; i < fds.size(); ++i) {
        const auto entry = capture_count.fetch_add(1, std::memory_order_relaxed);
        if (entry >= capture_rows.size()) break;
        // socket_key is opaque. Retain its exact object bytes, not a forged key.
        static_assert(std::is_trivially_copyable_v<srv::socket_key>);
        static_assert(sizeof(srv::socket_key) == sizeof(std::uint64_t));
        std::uint64_t key_bytes = 0;
        std::memcpy(&key_bytes, &interests[i].key, sizeof(key_bytes));
        int type = 0, size = sizeof(type);
        const int valid_rc = ::getsockopt(fds[i].fd, SOL_SOCKET, SO_TYPE,
            reinterpret_cast<char*>(&type), &size);
        const int valid_error = valid_rc < 0 ? ::WSAGetLastError() : 0;
        auto row = capture_stamp();
        std::snprintf(row.detail, sizeof(row.detail),
            "wait_fd index=%" PRIu64 " handle=%" PRIu64 " events=%d revents=%d key_bytes=%016" PRIx64
            " generation=%" PRIu64 " declared_valid=%d kind=%u validity_rc=%d validity_error=%d type=%d",
            static_cast<std::uint64_t>(i), static_cast<std::uint64_t>(fds[i].fd),
            fds[i].events, fds[i].revents, key_bytes,
            static_cast<std::uint64_t>(interests[i].generation), interests[i].handle.valid,
            static_cast<unsigned>(interests[i].handle.kind), valid_rc, valid_error, type);
        capture_rows[entry] = row;
        capture_emit_early(row);
    }
}
#endif

inline void capture_flush() noexcept {
    // Both writers have finished. Sorting pointers cannot allocate.
    std::array<const capture_row*, 80> rows{};
    std::size_t count = 0;
    for (const auto& row : capture_rows) if (row.sequence) rows[count++] = &row;
    for (const auto& row : capture_dispatch_rows) if (row.sequence) rows[count++] = &row;
    std::sort(rows.begin(), rows.begin() + count, [](const auto* left, const auto* right) {
        return left->sequence < right->sequence;
    });
#if defined(_WIN32)
    LARGE_INTEGER frequency{};
    ::QueryPerformanceFrequency(&frequency);
    std::fprintf(stderr, "WAIT_CAPTURE clock=QPC frequency=%" PRId64 " rows=%" PRIu64 " dropped=%u\n",
        static_cast<std::int64_t>(frequency.QuadPart), static_cast<std::uint64_t>(count),
        capture_count.load() > 64 ? capture_count.load() - 64 : 0);
#endif
    for (std::size_t i = 0; i < count; ++i) {
        std::fprintf(stderr, "WAIT_CAPTURE seq=%" PRIu64 " ticks=%" PRId64 " %s\n",
            rows[i]->sequence, rows[i]->ticks, rows[i]->detail);
    }
}

inline slot registration(const srv::socket_interest& interest) {
#if defined(_WIN32)
    if (!interest.handle.valid || interest.handle.kind != srv::native_handle_kind::winsock_socket) {
        throw std::runtime_error("expected a valid Winsock registration");
    }
    const auto fd = static_cast<SOCKET>(interest.handle.value);
#else
    if (!interest.handle.valid || interest.handle.kind != srv::native_handle_kind::posix_descriptor
            || interest.handle.value > INT_MAX) {
        throw std::runtime_error("expected a valid POSIX registration");
    }
    const auto fd = static_cast<int>(interest.handle.value);
#endif
    const std::int16_t mask = static_cast<std::int16_t>((interest.readable ? readable : 0)
                                         | (interest.writable ? writable : 0));
    return {fd, mask, 0};
}

inline int timeout(std::optional<clock_type::time_point> deadline) {
    if (!deadline) return -1;
    const auto now = clock_type::now();
    if (*deadline <= now) return 0;
    const auto gap = std::chrono::ceil<std::chrono::milliseconds>(*deadline - now).count();
    return static_cast<int>(std::min<decltype(gap)>(gap, INT_MAX));
}

// Rebuild the complete poll set on every call; keys/generations travel
// alongside native registrations and every queued callback keeps its pair.
// limit is an optional overall invocation/test budget, never an idle tick.
inline std::vector<srv::readiness_event> wait(
    const srv::interest_snapshot& snapshot,
    std::optional<clock_type::time_point> limit = {}) {
    auto deadline = snapshot.next_deadline;
    if (limit && (!deadline || *limit < *deadline)) deadline = limit;
    if (deadline && *deadline <= clock_type::now()) return {};
    auto interests = snapshot.sockets;
    if (snapshot.wake) interests.push_back(*snapshot.wake);
    std::vector<slot> fds;
    for (const auto& interest : interests) fds.push_back(registration(interest));
#if defined(_WIN32)
    const auto ready = ::WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeout(deadline));
    const int native_error = ready < 0 ? ::WSAGetLastError() : 0;
    if (ready < 0) capture_wait_failure(ready, native_error, fds, interests);
    if (ready < 0 && native_error != WSAEINTR) throw std::runtime_error("WSAPoll failed");
#else
    const auto ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout(deadline));
    if (ready < 0 && errno != EINTR) throw std::runtime_error("poll failed");
#endif
    std::vector<srv::readiness_event> events;
    for (std::size_t i = 0; i < fds.size(); ++i) {
        const std::int16_t seen = fds[i].revents;
        if (!seen) continue;
        events.push_back({interests[i].key, interests[i].generation,
            (seen & readable) != 0, (seen & writable) != 0,
            (seen & POLLHUP) != 0, (seen & (POLLERR | POLLNVAL)) != 0});
    }
    return events;
}
}  // namespace v3_host

#endif  // EXAMPLES_HOST_POLL_HPP_
