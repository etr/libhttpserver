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

#ifndef SRC_HTTPSERVER_SERVER_BUDGETS_HPP_
#define SRC_HTTPSERVER_SERVER_BUDGETS_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace httpserver {

namespace server {

// Accounting keys for the hierarchical limits (architecture §3.4):
// server, listener, connection, and stream budgets over the resource
// kinds below. The byte-valued kinds count aggregate bytes in flight
// for their scope; the count-valued kinds cap concurrent objects. The
// ws_-prefixed kind budgets messages of the upgrade-based subprotocol
// (RFC 6455). The set is libhttpserver-owned (DR-V3-001); no backend
// or OS numeric identifiers appear here.
enum class resource : std::uint8_t {
    connections,
    streams,
    header_bytes,
    header_fields,
    body_buffer_bytes,
    response_queue_bytes,
    hpack_table_bytes,
    qpack_table_bytes,
    blocked_headers,
    quic_reassembly_bytes,
    ws_message_bytes,
    timers,
    routes,
};

// Number of resource kinds; covers every enumerator.
inline constexpr std::size_t resource_count = 13;

// Stable diagnostic names, indexed by resource.
inline constexpr std::array<std::string_view, resource_count> resource_names = {
    "connections",         "streams",            "header_bytes",
    "header_fields",       "body_buffer_bytes",  "response_queue_bytes",
    "hpack_table_bytes",   "qpack_table_bytes",  "blocked_headers",
    "quic_reassembly_bytes", "ws_message_bytes", "timers",
    "routes",
};

// Human-readable name of a resource kind; empty for out-of-range values
// (total over the whole uint8_t domain).
constexpr std::string_view to_string(resource kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < resource_count ? resource_names[index]
                                  : std::string_view{};
}

// Documented per-kind capacity ceiling enforced by configuration
// validation at server scope and by child-budget construction at every
// nested scope. Out-of-range kinds yield 0 (total over uint8_t).
constexpr std::size_t max_capacity(resource kind) noexcept {
    constexpr std::array<std::size_t, resource_count> ceilings = {
        1048576,    // connections
        1048576,    // streams
        268435456,  // header_bytes
        65536,      // header_fields
        1073741824,  // body_buffer_bytes
        1073741824,  // response_queue_bytes
        16777216,   // hpack_table_bytes
        16777216,   // qpack_table_bytes
        65536,      // blocked_headers
        268435456,  // quic_reassembly_bytes
        268435456,  // ws_message_bytes
        1048576,    // timers
        1048576,    // routes
    };
    const auto index = static_cast<std::size_t>(kind);
    return index < resource_count ? ceilings[index] : 0;
}

// Per-kind capacity installed by a default-constructed budget_limits.
// Conservative production defaults, all within the ceilings above.
constexpr std::size_t default_capacity(resource kind) noexcept {
    constexpr std::array<std::size_t, resource_count> defaults = {
        1024,       // connections
        4096,       // streams
        1048576,    // header_bytes
        256,        // header_fields
        8388608,    // body_buffer_bytes
        4194304,    // response_queue_bytes
        65536,      // hpack_table_bytes
        65536,      // qpack_table_bytes
        256,        // blocked_headers
        1048576,    // quic_reassembly_bytes
        1048576,    // ws_message_bytes
        1024,       // timers
        1024,       // routes
    };
    const auto index = static_cast<std::size_t>(kind);
    return index < resource_count ? defaults[index] : 0;
}

// Capacity per resource kind for one scope of the server > listener >
// connection > stream hierarchy. Default construction installs
// default_capacity for every kind; get/set read and overwrite one kind.
// Scope bounds (a child capacity may not exceed its parent's) are
// enforced when budgets are nested, not here.
class budget_limits {
 public:
    constexpr budget_limits() noexcept {
        for (std::size_t i = 0; i < resource_count; ++i) {
            capacities_[i] = default_capacity(static_cast<resource>(i));
        }
    }

    constexpr std::size_t get(resource kind) const noexcept {
        return capacities_[static_cast<std::size_t>(kind)];
    }

    constexpr void set(resource kind, std::size_t capacity) noexcept {
        capacities_[static_cast<std::size_t>(kind)] = capacity;
    }

 private:
    std::array<std::size_t, resource_count> capacities_{};
};

}  // namespace server

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_SERVER_BUDGETS_HPP_
