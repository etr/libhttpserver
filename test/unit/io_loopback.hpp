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

// TASK-100: cross-platform loopback plumbing for the poll-driver socket
// scenarios. Every syscall routes through the pollsys shims so the
// _WIN32 split lives in exactly one place (io_poll_sys.hpp); this
// header holds test-side policy only: which end of a pair is adopted by
// the driver (nonblocking) and which stays blocking for the test
// thread, plus deadline-bounded blocking stream helpers.
//
// Loopback semantics relied on by the scenarios: closing one end makes
// the other end's recv report EOF (pollsys read => closed_reset), and
// writes into a connection whose peer vanished without reading
// eventually fail with the reset family -- on both the AF_UNIX pair
// (POSIX) and the 127.0.0.1 TCP pair (Windows).

#ifndef TEST_UNIT_IO_LOOPBACK_HPP_
#define TEST_UNIT_IO_LOOPBACK_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include "./httpserver/detail/io_poll_sys.hpp"

namespace io_loopback {

namespace pollsys = httpserver::detail::pollsys;

using pollsys::native_socket_t;

// Deadline budget for the blocking helpers below. A healthy loopback
// transfers in microseconds; this only bounds a broken scenario.
constexpr std::chrono::milliseconds kIoBudget{5000};

// A connected socket pair. `local` is the end the driver adopts (set
// nonblocking, prepared); `peer` stays blocking for the test thread.
class pair {
 public:
    pair() = default;

    pair(const pair&) = delete;
    pair& operator=(const pair&) = delete;

    pair(pair&& other) noexcept
        : local_(std::exchange(other.local_, pollsys::k_invalid_socket)),
          peer_(std::exchange(other.peer_, pollsys::k_invalid_socket)) { }

    pair& operator=(pair&& other) noexcept {
        if (this != &other) {
            close_both();
            local_ = std::exchange(other.local_, pollsys::k_invalid_socket);
            peer_ = std::exchange(other.peer_, pollsys::k_invalid_socket);
        }
        return *this;
    }

    ~pair() { close_both(); }

    static pair make() {
        native_socket_t ends[2] = {pollsys::k_invalid_socket,
                                   pollsys::k_invalid_socket};
        pair p;
        if (!pollsys::make_loopback_pair(ends)) {
            return p;
        }
        p.local_ = ends[0];
        p.peer_ = ends[1];
        pollsys::set_nonblocking(p.local_, true);
        pollsys::prepare_stream_socket(p.local_);
        pollsys::prepare_stream_socket(p.peer_);
        return p;
    }

    bool ok() const {
        return local_ != pollsys::k_invalid_socket
               && peer_ != pollsys::k_invalid_socket;
    }

    native_socket_t local() const { return local_; }
    native_socket_t peer() const { return peer_; }

    // Relinquishes the adopted end WITHOUT closing it: from adoption on
    // the backend record owns the handle, so exactly one close happens
    // (release_connection or backend destruction), never two.
    native_socket_t detach_local() {
        native_socket_t taken = local_;
        local_ = pollsys::k_invalid_socket;
        return taken;
    }

    void close_peer() {
        pollsys::close_socket(peer_);
        peer_ = pollsys::k_invalid_socket;
    }

    void close_local() {
        pollsys::close_socket(local_);
        local_ = pollsys::k_invalid_socket;
    }

    void close_both() {
        close_local();
        close_peer();
    }

 private:
    native_socket_t local_ = pollsys::k_invalid_socket;
    native_socket_t peer_ = pollsys::k_invalid_socket;
};

// A listener bound on the loopback address with an ephemeral port, in
// the nonblocking prepared state the driver contract requires.
class listener {
 public:
    listener() = default;

    listener(const listener&) = delete;
    listener& operator=(const listener&) = delete;

    listener(listener&& other) noexcept
        : socket_(std::exchange(other.socket_, pollsys::k_invalid_socket)),
          port_(other.port_) { }

    listener& operator=(listener&& other) noexcept {
        if (this != &other) {
            close();
            socket_ = std::exchange(other.socket_,
                                    pollsys::k_invalid_socket);
            port_ = other.port_;
        }
        return *this;
    }

    ~listener() { pollsys::close_socket(socket_); }

    static listener open() {
        listener l;
        l.socket_ = pollsys::make_listener(l.port_);
        return l;
    }

    bool ok() const { return socket_ != pollsys::k_invalid_socket; }
    native_socket_t socket() const { return socket_; }
    std::uint16_t port() const { return port_; }

    // Relinquishes the handle without closing it (backend ownership
    // after adopt_listener); the port stays available for clients.
    void detach() { socket_ = pollsys::k_invalid_socket; }

    // Closes without destroying (keeps the port value for logging).
    void close() {
        pollsys::close_socket(socket_);
        socket_ = pollsys::k_invalid_socket;
    }

 private:
    native_socket_t socket_ = pollsys::k_invalid_socket;
    std::uint16_t port_ = 0;
};

// Blocking connect to a loopback port (0 means "no listener"): returns
// a connected blocking socket, or k_invalid_socket on failure.
inline native_socket_t connect_to(std::uint16_t port) {
    if (port == 0) {
        return pollsys::k_invalid_socket;
    }
    native_socket_t client = pollsys::open_stream();
    if (client == pollsys::k_invalid_socket) {
        return pollsys::k_invalid_socket;
    }
    if (!pollsys::connect_loopback(client, port)) {
        pollsys::close_socket(client);
        return pollsys::k_invalid_socket;
    }
    pollsys::prepare_stream_socket(client);
    return client;
}

// Blocking send of all bytes; gives up (silently, scenario-visible)
// past kIoBudget.
inline void write_all(native_socket_t socket, const std::byte* data,
                      std::size_t size) {
    std::size_t sent = 0;
    const auto give_up = std::chrono::steady_clock::now() + kIoBudget;
    while (sent < size && std::chrono::steady_clock::now() < give_up) {
        const pollsys::sys_result r =
            pollsys::write_some(socket, data + sent, size - sent);
        if (r.status == pollsys::sys_status::ok) {
            sent += r.transferred;
        } else if (r.status != pollsys::sys_status::would_block) {
            return;  // hangup family: the scenario observes it via ops
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
}

inline void write_all(native_socket_t socket, const void* data,
                      std::size_t size) {
    write_all(socket, static_cast<const std::byte*>(data), size);
}

// Blocking receive of exactly size bytes; false past kIoBudget.
inline bool read_exact(native_socket_t socket, std::byte* data,
                       std::size_t size) {
    std::size_t got = 0;
    const auto give_up = std::chrono::steady_clock::now() + kIoBudget;
    while (got < size && std::chrono::steady_clock::now() < give_up) {
        const pollsys::sys_result r =
            pollsys::read_some(socket, data + got, size - got);
        if (r.status == pollsys::sys_status::ok) {
            got += r.transferred;
        } else if (r.status != pollsys::sys_status::would_block) {
            return false;  // EOF/reset before the payload completed
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    return got == size;
}

// Reads and discards whatever is queued without blocking (nonblocking
// end expected); used to drain stimulus leftovers between scenarios.
inline void drain(native_socket_t socket) {
    if (socket == pollsys::k_invalid_socket) {
        return;
    }
    std::byte scratch[512];
    for (;;) {
        const pollsys::sys_result r =
            pollsys::read_some(socket, scratch, sizeof(scratch));
        if (r.status != pollsys::sys_status::ok
            || r.transferred == 0) {
            return;
        }
    }
}

}  // namespace io_loopback

#endif  // TEST_UNIT_IO_LOOPBACK_HPP_
