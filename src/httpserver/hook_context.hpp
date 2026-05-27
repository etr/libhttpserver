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

/**
 * @file hook_context.hpp
 * @brief Per-phase context structs passed to hook callables.
 *
 * TASK-045 / DR-012 / §4.10. Each `*_ctx` type carries exactly the
 * information the matching phase publishes; lifetime is the hook
 * callback invocation -- pointers in these structs MUST NOT be
 * captured past the callback's return. See
 * `specs/architecture/04-components/hooks.md` for the field-by-field
 * contract.
 */
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

/**
 * @brief Libhttpserver-defined peer address (IPv4 or IPv6) plus port.
 *
 * Kept libhttpserver-native so the public hook surface carries no
 * `<sys/socket.h>` or MHD types (PRD-HDR-REQ-001). `bytes` is in
 * network byte order: the first four bytes carry an IPv4 address with
 * the rest zero; all sixteen bytes are used for IPv6. `port` is in
 * host byte order.
 */
struct peer_address {
    enum class family : std::uint8_t { unspec = 0, ipv4 = 1, ipv6 = 2 };
    family fam = family::unspec;
    std::array<std::uint8_t, 16> bytes{};
    std::uint16_t port = 0;

    // Returns a printable representation of the address (no port).
    // Defined out-of-line in src/hook_handle.cpp.
    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief Light pointer-and-bag view of a matched route.
 *
 * Used by `route_resolved_ctx` and `before_handler_ctx`.
 * `path_template` is a view into the registered route's stored string
 * and is valid for the lifetime of the registration. `methods`
 * carries the method bits the matched entry serves; `is_prefix` flags
 * prefix-match registrations (`register_prefix` / single-resource).
 */
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

/// @brief Context for the `connection_opened` phase: new TCP/TLS connection.
struct connection_open_ctx {
    peer_address peer{};
};

/// @brief Context for the `connection_closed` phase: connection torn down.
struct connection_close_ctx {
    peer_address peer{};
};

/**
 * @brief Context for the `accept_decision` phase.
 *
 * Observation-only per DR-012; the handler returns `void`. Accept/deny
 * is decided by the policy callback, not by the hook.
 *
 * @note `accepted` mirrors the policy callback's MHD_YES/MHD_NO return.
 * @note `reason` is set when the connection is rejected:
 *   - `"banned"` — the peer hit the ban list.
 *   - `"not-allowed"` — default policy REJECT and the peer is not on
 *     the allowance list.
 *   - `std::nullopt` — the connection was accepted.
 * @note The `string_view`'s referent is a string literal with static
 *   storage duration; capturing it past the hook return is safe. If a
 *   heap-owned copy is needed, use `std::string(*ctx.reason)`.
 */
struct accept_ctx {
    peer_address peer{};
    bool accepted = true;
    std::optional<std::string_view> reason{};
};

/**
 * @brief Context for the `request_received` phase.
 *
 * Fires after the `http_request` is fully populated from MHD's
 * headers and BEFORE any body bytes are read. The request pointer is
 * mutable so a hook may adjust per-request state before the upload
 * starts. Short-circuit-capable: returning
 * `hook_action::respond_with(r)` aborts the upload -- the body is
 * never read and the resource handler is never invoked.
 */
struct request_received_ctx {
    http_request* request = nullptr;          // mutable: hook may set context
    std::chrono::steady_clock::time_point received_at{};
};

/**
 * @brief Context for the `body_chunk` phase.
 *
 * Fires once per chunk MHD delivers to the upload callback, BEFORE
 * the bytes are appended to the request body or fed to any in-flight
 * post-processor. Short-circuit-capable.
 *
 * @attention This phase is invoked from arbitrary MHD worker threads
 *   at arbitrary granularity — on slow networks chunks may be a single
 *   byte. Hooks MUST be cheap (no blocking I/O, no per-chunk heap
 *   allocation in the hot path) — a slow hook back-pressures the
 *   connection's upload.
 * @note `chunk` aliases MHD-owned memory; it is only valid for the
 *   duration of the hook call. Copy into owned storage if the data must
 *   outlive this firing.
 * @note `offset` is the number of body bytes already buffered before
 *   this chunk (first firing has `offset==0`, next has
 *   `offset==chunk0.size()`, etc.).
 * @note Short-circuit: returning `hook_action::respond_with(r)` aborts
 *   the upload at the next MHD callback; the resource handler is never
 *   invoked. Any in-flight post-processor is destroyed and its buffer
 *   freed at the short-circuit point.
 */
struct body_chunk_ctx {
    http_request* request = nullptr;
    std::span<const std::byte> chunk{};
    std::uint64_t offset = 0;
    bool is_final = false;
};

/**
 * @brief Context for the `route_resolved` phase.
 *
 * Observation-only. `matched` is empty for the 404 path (no route
 * matched); otherwise it describes the matched registration.
 */
struct route_resolved_ctx {
    const http_request* request = nullptr;
    std::optional<route_descriptor> matched{};
    const http_resource* resource = nullptr;  // nullable; nullptr for lambda routes
};

/**
 * @brief Context for the `before_handler` phase.
 *
 * Short-circuit-capable. Fires after route resolution (the matched
 * resource is known) and BEFORE both `is_allowed` and the resource
 * handler invocation. Returning `hook_action::respond_with(r)` skips
 * both checks and goes straight to response materialisation. Also
 * the phase used by the `method_not_allowed_handler` and
 * `auth_handler` v1 aliases.
 *
 * @note `method` is the wire method decoded by `answer_to_connection`.
 *   The 405-alias hook consults this against
 *   `resource->get_allowed_methods()` to decide whether to
 *   short-circuit with 405 + Allow header.
 * @note `resource` is the resolved `http_resource` pointer; `nullptr`
 *   for lambda-route registrations without a stable `http_resource*`.
 *   The hook fires only for route hits (§4.10).
 */
struct before_handler_ctx {
    http_request* request = nullptr;
    std::optional<route_descriptor> matched{};
    http_method method = http_method::count_;
    const http_resource* resource = nullptr;
};

/**
 * @brief Context for the `handler_exception` phase.
 *
 * Short-circuit-capable. Fires when an exception escapes the resource
 * handler, before the `internal_error_handler` v1 alias is consulted.
 */
struct handler_exception_ctx {
    const http_request* request = nullptr;
    std::exception_ptr exception{};
    std::string_view message{};
};

/**
 * @brief Context for the `after_handler` phase.
 *
 * Short-circuit-capable; the `response` pointer is mutable so hooks
 * may rewrite headers / status without replacing the body.
 */
struct after_handler_ctx {
    const http_request* request = nullptr;
    http_response* response = nullptr;        // mutable: hook may mutate
};

/**
 * @brief Context for the `response_sent` phase.
 *
 * Observation point fired immediately after `MHD_queue_response` and
 * BEFORE `MHD_destroy_response`. Carries the data users have been
 * asking for (issues #281 and #69): `status`, `bytes_queued`,
 * `elapsed`. The `log_access` v1 alias is wired through this phase.
 *
 * @note `status` is the HTTP status code passed to
 *   `MHD_queue_response`.
 * @note `bytes_queued` is `http_response::body_->size()`. For deferred
 *   or pipe bodies `size()` returns 0 because the final length is not
 *   yet known at queue time; fall back to the Content-Length header for
 *   streamed bodies.
 * @note `elapsed` is `steady_clock::now()` at the fire site minus
 *   `modded_request::start_time` (captured on the first invocation of
 *   `answer_to_connection`). Granularity is nanoseconds.
 * @attention The `response` pointer is non-null at the fire site.
 *   Hooks MUST NOT capture it past their return — the `http_response`
 *   is destroyed in `~modded_request` immediately after
 *   `request_completed` fires.
 */
struct response_sent_ctx {
    const http_request* request = nullptr;
    const http_response* response = nullptr;
    int status = 0;
    std::size_t bytes_queued = 0;
    std::chrono::nanoseconds elapsed{};
};

/**
 * @brief Context for the `request_completed` phase.
 *
 * Unconditional final hook. Fires BEFORE the per-request state is
 * destroyed so the ctx pointers remain backed by live storage for the
 * duration of the hook call. Hooks MUST NOT capture `request` or
 * `resp` past their return.
 *
 * @note `resp` is NULLABLE. On early-failure paths (e.g., a
 *   `request_received` hook returning `respond_with(413)`),
 *   `mr->response_` is populated and `resp` points into it. On paths
 *   where MHD terminates the request before any response object is
 *   built, `resp` is `nullptr`.
 * @note `succeeded` maps from `MHD_RequestTerminationCode`:
 *   `MHD_REQUEST_TERMINATED_COMPLETED_OK` → `true`; everything else →
 *   `false`. A user-policy rejection that produced a complete response
 *   on the wire (e.g., a 413 from a `request_received` short-circuit)
 *   reports `succeeded == true` because MHD drove the request to
 *   ordinary completion.
 * @note `duration` is `steady_clock::now()` at the fire site minus
 *   `modded_request::start_time`; mirrors `response_sent_ctx::elapsed`.
 * @attention Hooks MUST NOT capture `request` or `resp` past their
 *   return — both are destroyed in `~modded_request` immediately after
 *   this fire.
 */
struct request_completed_ctx {
    const http_request* request = nullptr;
    const http_response* resp = nullptr;
    bool succeeded = false;
    std::chrono::steady_clock::duration duration{};
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HOOK_CONTEXT_HPP_
