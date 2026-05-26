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
//
// TASK-046 extends the TASK-045 skeleton with the decision the policy
// callback already computed before the hook fires:
//   - `accepted` mirrors policy_callback's MHD_YES/MHD_NO return.
//   - `reason` is set when the connection is rejected:
//       * `"banned"`      — the peer hit the ban list.
//       * `"not-allowed"` — default policy REJECT and the peer is not
//                            on the allowance list.
//       * `std::nullopt`  — the connection was accepted.
//
// The string_view's referent is always a string literal with static
// storage duration; capturing it past the hook return is safe. If a
// heap-owned copy is needed, materialize `std::string(*ctx.reason)`.
struct accept_ctx {
    peer_address peer{};
    bool accepted = true;
    std::optional<std::string_view> reason{};
};

// request_received: fires after the http_request is fully populated
// from MHD's headers and BEFORE any body bytes are read. The request
// pointer is mutable so a hook may adjust per-request state (e.g.
// content-size limit) before the upload starts. Short-circuit:
// returning hook_action::respond_with(r) aborts the upload -- the body
// is never read and the resource handler is never invoked.
struct request_received_ctx {
    http_request* request = nullptr;          // mutable: hook may set context
    std::chrono::steady_clock::time_point received_at{};
};

// body_chunk: fires once per chunk MHD delivers to the upload
// callback, BEFORE the bytes are appended to the request body or fed
// to any in-flight post-processor.
//
// IMPORTANT: this phase is invoked from arbitrary MHD worker threads
// at arbitrary granularity -- on slow networks chunks may be a single
// byte. Hooks MUST be cheap (no blocking I/O, no per-chunk heap
// allocation in the hot path) -- a slow hook back-pressures the
// connection's upload. The `chunk` span aliases MHD-owned memory and
// is only valid for the duration of the hook call; copy into owned
// storage if it must outlive this firing. `offset` is the number of
// body bytes already buffered (so the first firing has offset==0,
// the next has offset==chunk0.size(), etc.).
//
// Short-circuit: returning hook_action::respond_with(r) aborts the
// upload at the next MHD callback and the resource handler is never
// invoked. Any in-flight post-processor is destroyed and its buffer
// freed at the short-circuit point (so registered handlers cannot
// observe a half-populated form).
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

// before_handler: short-circuit-capable. Fires after route resolution
// (the matched resource is known) and BEFORE both is_allowed and the
// resource handler invocation. Returning hook_action::respond_with(r)
// skips both checks and goes straight to response materialisation.
//
// TASK-048 extends the TASK-045 skeleton with `method` and `resource`:
//   - `method` is the wire method already decoded by
//     answer_to_connection (mr->method_enum). The 405-alias hook
//     consults this against `resource->get_allowed_methods()` to
//     decide whether to short-circuit with 405 + Allow header.
//   - `resource` is the resolved http_resource pointer; nullptr for
//     route misses (the hook fires only for hits — see §4.10) and
//     for lambda-route registrations exposed without a stable
//     http_resource*.
struct before_handler_ctx {
    http_request* request = nullptr;
    std::optional<route_descriptor> matched{};
    http_method method = http_method::count_;
    const http_resource* resource = nullptr;
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

// response_sent: observation point fired immediately after
// MHD_queue_response and BEFORE MHD_destroy_response. Carries the data
// users have been asking for (issues #281 and #69):
//
//   - status:        the HTTP status code MHD was handed (the value
//                    passed to MHD_queue_response).
//   - bytes_queued:  http_response::body_->size(). For deferred / pipe
//                    bodies, body::size() returns 0 because the final
//                    length is not yet known at queue time -- consumers
//                    that need byte counts for streamed bodies should
//                    fall back to the Content-Length header value.
//   - elapsed:       std::chrono::steady_clock::now() at the fire site
//                    minus modded_request::start_time (captured on the
//                    first invocation of answer_to_connection for this
//                    request -- the earliest moment we can measure).
//                    Granularity is nanoseconds.
//
// The `response` pointer is non-null at the fire site (the dispatch
// path guarantees a response value lives in mr->response_ by the time
// materialize_and_queue_response is called). Hooks MUST NOT capture
// this pointer past their return: the http_response is destroyed in
// ~modded_request right after request_completed fires.
struct response_sent_ctx {
    const http_request* request = nullptr;
    const http_response* response = nullptr;
    int status = 0;
    std::size_t bytes_queued = 0;
    std::chrono::nanoseconds elapsed{};
};

// request_completed: unconditional final hook. Fires BEFORE the
// modded_request is destroyed so the ctx pointers remain backed by
// live storage for the duration of the hook call.
//
//   - resp:       NULLABLE. On early-failure paths (e.g., a
//                 request_received hook returning respond_with(413)),
//                 mr->response_ is populated and resp points into it.
//                 On paths where MHD terminates the request before any
//                 response object is built (very early MHD failures),
//                 resp is nullptr.
//   - succeeded:  maps from MHD_RequestTerminationCode:
//                   * MHD_REQUEST_TERMINATED_COMPLETED_OK -> true
//                   * everything else                     -> false
//                 A user-policy rejection that produced a complete
//                 response on the wire (e.g., a 413 from a
//                 request_received short-circuit) reports succeeded
//                 == true: MHD still drove the request to ordinary
//                 completion -- the rejection happened in libhttpserver,
//                 not in MHD's transport.
//   - duration:   steady_clock::now() at the fire site minus
//                 modded_request::start_time. Kept for back-compat
//                 with the TASK-045 shape; mirrors response_sent's
//                 elapsed measurement.
//
// Hooks MUST NOT capture `request` or `resp` past their return -- both
// are destroyed in ~modded_request immediately after this fire.
struct request_completed_ctx {
    const http_request* request = nullptr;
    const http_response* resp = nullptr;
    bool succeeded = false;
    std::chrono::steady_clock::duration duration{};
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_CONTEXT_HPP_
