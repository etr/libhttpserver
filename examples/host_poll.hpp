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
#else
#include <poll.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
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

#if defined(_WIN32) && defined(LIBHTTPSERVER_REMAINING_WAIT_CAPTURE)
inline std::atomic<unsigned> stop_bits{0};
inline void capture_stop_phase(unsigned bit) { stop_bits.fetch_or(bit); }
inline void capture_stop_receipt(bool failed, bool running) {
    std::fprintf(stderr, "STOP_CAPTURE bits=%u failed=%d running=%d\n", stop_bits.load(), failed, running);
}
inline void capture_wait_error(const std::vector<slot>& fds, int saved_error) {
    LARGE_INTEGER ticks{};
    ::QueryPerformanceCounter(&ticks);
    std::fprintf(stderr, "WSA_WAIT_ERROR code=%d stop_bits=%u ticks=%lld count=%zu\n",
        saved_error, stop_bits.load(), ticks.QuadPart, fds.size());
    for (const auto& fd : fds) {
        int type = 0, error = 0, length = sizeof(type);
        const int type_rc = ::getsockopt(fd.fd, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &length);
        const int type_error = type_rc < 0 ? ::WSAGetLastError() : 0;
        length = sizeof(error);
        const int error_rc = ::getsockopt(fd.fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
        const int error_error = error_rc < 0 ? ::WSAGetLastError() : 0;
        sockaddr_storage local{}, peer{};
        length = sizeof(local);
        const int local_rc = ::getsockname(fd.fd, reinterpret_cast<sockaddr*>(&local), &length);
        const int local_error = local_rc < 0 ? ::WSAGetLastError() : 0;
        length = sizeof(peer);
        const int peer_rc = ::getpeername(fd.fd, reinterpret_cast<sockaddr*>(&peer), &length);
        const int peer_error = peer_rc < 0 ? ::WSAGetLastError() : 0;
        std::fprintf(stderr, "WSA_WAIT_SOCKET fd=%zu events=%d revents=%d type=%d type_rc=%d type_error=%d "
            "socket_error=%d error_rc=%d error_error=%d local_rc=%d local_error=%d local_family=%d "
            "peer_rc=%d peer_error=%d peer_family=%d\n", static_cast<std::size_t>(fd.fd), fd.events,
            fd.revents, type, type_rc, type_error, error, error_rc, error_error, local_rc, local_error,
            local.ss_family, peer_rc, peer_error, peer.ss_family);
    }
}
#else
inline void capture_stop_phase(unsigned) { }
inline void capture_stop_receipt(bool, bool) { }
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
    if (ready < 0) {
        const int saved_error = ::WSAGetLastError();
        if (saved_error != WSAEINTR) {
#if defined(LIBHTTPSERVER_REMAINING_WAIT_CAPTURE)
            capture_wait_error(fds, saved_error);
#endif
            throw std::runtime_error("WSAPoll failed");
        }
    }
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
