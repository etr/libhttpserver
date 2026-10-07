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

#ifndef EXAMPLES_V3_HOST_POLL_HPP_
#define EXAMPLES_V3_HOST_POLL_HPP_

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
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
    if (ready < 0 && ::WSAGetLastError() != WSAEINTR) throw std::runtime_error("WSAPoll failed");
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

#endif  // EXAMPLES_V3_HOST_POLL_HPP_
