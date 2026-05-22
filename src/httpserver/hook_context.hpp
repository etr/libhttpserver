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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_HOOK_CONTEXT_HPP_
#define SRC_HTTPSERVER_HOOK_CONTEXT_HPP_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "httpserver/http_method.hpp"

namespace httpserver {

// Forward declarations -- the hook contexts reference http_request and
// http_response only by pointer/reference, so we avoid pulling either
// header into this public surface (PRD-HDR-REQ-001: no transitive
// backend leak). http_resource is referenced for the route-resolved
// phase via an optional pointer; same forward-decl strategy.
class http_request;
class http_response;
class http_resource;

// TASK-045 / §4.10 / DR-012.
//
// peer_address is libhttpserver-defined to keep all <sys/socket.h> and
// MHD types out of the public hook surface (PRD-HDR-REQ-001). TASK-046
// will construct this struct from the `const struct sockaddr*` MHD
// hands to the accept-decision callback, inside webserver.cpp where
// <sys/socket.h> is already in scope.
//
// `bytes` is in network byte order (big-endian). For IPv4 the first
// four bytes carry the address and the rest are zero; for IPv6 all
// sixteen bytes are used. `port` is in host byte order.
struct peer_address {
    enum class family : std::uint8_t { unspec = 0, ipv4 = 1, ipv6 = 2 };
    family fam = family::unspec;
    std::array<std::uint8_t, 16> bytes{};
    std::uint16_t port = 0;

    // Returns a printable representation of the address (no port).
    // Defined out-of-line in src/hook_handle.cpp.
    [[nodiscard]] std::string to_string() const;
};

// route_descriptor: light pointer-and-bag view of a matched route, used
// by route_resolved_ctx and the after-handler context. Definition pinned
// at TASK-045 so TASK-048 only has to populate it. `path_template` is a
// view into the registered route's stored string -- valid for the
// lifetime of the registration. `methods` carries the method bits the
// matched entry serves; `is_prefix` flags prefix-match registrations
// (register_prefix / single_resource).
struct route_descriptor {
    std::string_view path_template;
    method_set methods{};
    bool is_prefix = false;
};

// ---- Phase context structs ---------------------------------------------
//
// Each phase has a dedicated context struct so the per-phase add_hook
// overload is distinguishable from the other ten at the type level, and
// so adding fields to one phase does not perturb the others' ABI.
//
// TASK-045 pins the shape; TASK-046..051 will wire firing-site code that
// populates these fields. The fields below are deliberately POD-shaped
// (references / scalars / spans / string_views / optionals of POD).

struct connection_open_ctx {
    peer_address peer{};
};

struct connection_close_ctx {
    peer_address peer{};
};

// accept_decision: observation-only per DR-012; the handler returns
// void. accept/deny is decided by the policy callback, not by the hook.
struct accept_ctx {
    peer_address peer{};
};

struct request_received_ctx {
    http_request* request = nullptr;          // mutable: hook may set context
    std::chrono::steady_clock::time_point received_at{};
};

struct body_chunk_ctx {
    http_request* request = nullptr;
    std::span<const std::byte> chunk{};
    std::uint64_t offset = 0;
    bool is_final = false;
};

// route_resolved: observation-only. `matched` is empty for the 404 path
// (no route matched); otherwise it describes the matched registration.
struct route_resolved_ctx {
    const http_request* request = nullptr;
    std::optional<route_descriptor> matched{};
    const http_resource* resource = nullptr;  // nullable; nullptr for lambda routes
};

struct before_handler_ctx {
    http_request* request = nullptr;
    std::optional<route_descriptor> matched{};
};

struct handler_exception_ctx {
    const http_request* request = nullptr;
    std::exception_ptr exception{};
    std::string_view message{};
};

struct after_handler_ctx {
    const http_request* request = nullptr;
    http_response* response = nullptr;        // mutable: hook may mutate
};

struct response_sent_ctx {
    const http_request* request = nullptr;
    const http_response* response = nullptr;
    std::chrono::steady_clock::time_point sent_at{};
};

struct request_completed_ctx {
    const http_request* request = nullptr;
    std::chrono::steady_clock::duration duration{};
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_CONTEXT_HPP_
