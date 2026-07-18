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

// hook_dispatcher -- behavior service (DR-014, §4.11) owning the
// dispatch-time hook firing. Two layers:
//   * eleven thin per-phase forwarders (fire_connection_opened ...
//     fire_request_completed) that bind the error logger and delegate the
//     snapshot-under-lock + try/catch iteration to the hook_bus state
//     collaborator;
//   * four gated helpers (fire_before_handler_gated / _after_handler_gated
//     / _response_sent_gated / _request_completed_gated) that add the
//     per-request gating, context construction, and server-wide+per-route
//     chain sequencing carved out of the dispatch path.
//
// Holds hook_bus& (the phase vectors + alias slots) and const
// webserver_config& (only to reach log_dispatch_error). Owns no state,
// takes no locks of its own -- the hook_bus owns the shared_mutex.
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "hook_dispatcher.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HOOK_DISPATCHER_HPP_
#define SRC_HTTPSERVER_DETAIL_HOOK_DISPATCHER_HPP_

#include <microhttpd.h>

#include <memory>
#include <optional>

#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_response.hpp"

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

struct webserver_config;
class http_resource;

namespace detail {

struct modded_request;
class hook_bus;

class hook_dispatcher {
 public:
    hook_dispatcher(hook_bus& hooks, const webserver_config& config) noexcept
        : hooks_(hooks), config_(config) {}

    hook_dispatcher(const hook_dispatcher&) = delete;
    hook_dispatcher& operator=(const hook_dispatcher&) = delete;
    hook_dispatcher(hook_dispatcher&&) = delete;
    hook_dispatcher& operator=(hook_dispatcher&&) = delete;
    ~hook_dispatcher() = default;

    // ---- query forwarders to the underlying hook_bus (dispatch gating) ---
    bool has_hooks_for(hook_phase p) const noexcept;
    bool has_handler_exception_alias() const noexcept;

    // ---- eleven per-phase forwarders (bind the logger, delegate to bus) --
    void fire_connection_opened(const connection_open_ctx& ctx) noexcept;
    void fire_accept_decision(const accept_ctx& ctx) noexcept;
    void fire_connection_closed(const connection_close_ctx& ctx) noexcept;
    std::optional<http_response> fire_request_received(
        request_received_ctx& ctx) noexcept;
    std::optional<http_response> fire_body_chunk(
        body_chunk_ctx& ctx) noexcept;
    void fire_route_resolved(const route_resolved_ctx& ctx) noexcept;
    std::optional<http_response> fire_before_handler(
        before_handler_ctx& ctx) noexcept;
    std::optional<http_response> fire_handler_exception(
        const handler_exception_ctx& ctx) noexcept;
    std::optional<http_response> fire_after_handler(
        after_handler_ctx& ctx) noexcept;
    void fire_response_sent(const response_sent_ctx& ctx) noexcept;
    void fire_request_completed(const request_completed_ctx& ctx) noexcept;

    // ---- four gated helpers (per-request gating + chain sequencing) ------
    // Returns true iff a before_handler hook short-circuited (mr->response
    // already emplaced; caller routes straight to materialize).
    bool fire_before_handler_gated(
        modded_request* mr,
        const std::shared_ptr<http_resource>& hrm);
    void fire_after_handler_gated(modded_request* mr,
                                  http_resource* resource);
    void fire_response_sent_gated(modded_request* mr,
                                  http_resource* resource);
    void fire_request_completed_gated(modded_request* mr,
                                      enum MHD_RequestTerminationCode toe);

 private:
    hook_bus& hooks_;
    const webserver_config& config_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HOOK_DISPATCHER_HPP_
