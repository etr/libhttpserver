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

#ifndef SRC_HTTPSERVER_SERVER_READINESS_HPP_
#define SRC_HTTPSERVER_SERVER_READINESS_HPP_

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <httpserver/http/outcome.hpp>

namespace httpserver {
namespace server {

// Opaque library identity, independent of any native registration handle.
class socket_key {
 public:
    constexpr socket_key() noexcept = default;
    explicit constexpr socket_key(std::uint64_t identity) noexcept
        : identity_(identity) { }

    friend constexpr bool operator==(socket_key, socket_key) noexcept = default;

 private:
    std::uint64_t identity_ = 0;
};

using registration_generation = std::uint64_t;

enum class native_handle_kind : std::uint8_t {
    posix_descriptor,
    winsock_socket,
};

// Borrowed registration carrier. Zero is valid when valid is true; never
// truncate a Winsock carrier to int. Only the library closes these handles.
struct native_handle {
    std::uintptr_t value = 0;
    native_handle_kind kind = native_handle_kind::posix_descriptor;
    bool valid = false;
};

struct socket_interest {
    socket_key key;
    registration_generation generation = 0;
    native_handle handle;
    bool readable = false;
    bool writable = false;
};

// closed and error are portable indications, not native status numbers.
struct readiness_event {
    socket_key key;
    registration_generation generation = 0;
    bool readable = false;
    bool writable = false;
    bool closed = false;
    bool error = false;
};

// Complete, coherent, owned registration instructions. Omission unregisters.
// Copying a snapshot does not extend the borrowed handles' lifetimes.
struct interest_snapshot {
    std::vector<socket_interest> sockets;
    std::optional<socket_interest> wake;
    std::optional<std::chrono::steady_clock::time_point> next_deadline;
};

// Library-owned consumer port; there is no replacement-backend registration.
// The normative lifecycle/threading rules are in docs/external-loop-contract.md.
// TASK-125 supplies the server-owned adapter and enforces the runtime contract.
class readiness_driver {
 public:
    virtual ~readiness_driver() = default;

    // May throw std::bad_alloc. Fresh snapshots follow successful dispatch.
    virtual interest_snapshot interests() const = 0;

    // Never waits. Same-driver overlap/recursion returns invalid_state before
    // consuming events. Stale (key, generation) callbacks are ignored.
    virtual http::outcome dispatch(std::span<const readiness_event> events,
                                  std::chrono::steady_clock::time_point now) = 0;
};

}  // namespace server
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_READINESS_HPP_
