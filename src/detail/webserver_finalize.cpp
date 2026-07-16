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

// Gated hook-firing helpers for the after_handler, response_sent, and
// request_completed lifecycle phases. Carved out of webserver_request.cpp
// and webserver_callbacks.cpp to keep each TU under the project LOC
// ceiling. See per-function comments for behavioral details.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <microhttpd.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "httpserver/detail/body.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/resource_hook_table.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"

namespace httpserver {
namespace detail {

namespace {

// TASK-051: helper to fetch the per-route hook table (if any) from
// the request's resource_weak_ slot. Holds the shared_ptr<http_resource>
// alive in `res_out` so the caller can keep the table pointer valid.
// Returns nullptr when no per-route table exists for this request.
//
// Uses a direct lock() rather than expired()+lock() to avoid the TOCTOU
// window: lock() is atomic -- it either succeeds or returns null. The
// shared_ptr in res_out keeps the resource (and its hook table) alive
// for the duration of the caller's firing loop.
resource_hook_table* per_route_table(detail::modded_request* mr,
                                     std::shared_ptr<http_resource>& res_out) {
    res_out = mr->resource_weak_.lock();
    if (!res_out) return nullptr;
    return res_out->hook_table_raw_();
}

}  // namespace

// TASK-051: gated fire of before_handler, server-wide AND per-route,
// with short-circuit detection. Carved out of finalize_answer to keep
// the host function under CCN_MAX after TASK-051's per-route extension
// roughly doubled the local branch count. Returns true iff either chain
// short-circuited (mr->response has already been emplaced; caller must
// route straight to materialize_and_queue_response). False means both
// chains passed (or both gates were closed) and dispatch should proceed.
bool webserver_impl::fire_before_handler_gated(
        detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    const bool server_gate = has_hooks_for(hook_phase::before_handler);
    // Note: rtable comes from hrm directly (already resolved from the
    // route table lock); no need for the per_route_table() helper here.
    // hrm keeps the resource alive for the duration of this function.
    //
    // Memory-order: hrm was acquired from resolve_resource_for_request
    // under the route_table_mutex_ shared_lock, which provides the acquire
    // barrier needed to observe the hook_table_ pointer written by any prior
    // add_hook() call (ensure_table stores with acq_rel). hook_table_raw_()
    // reads the shared_ptr with the guarantee inherited from that acquire.
    auto* rtable = hrm->hook_table_raw_();
    const bool per_route_gate = rtable != nullptr &&
        rtable->any_hooks(hook_phase::before_handler);
    if (!server_gate && !per_route_gate) return false;

    std::optional<route_descriptor> desc;
    if (!mr->matched_path_template.empty()) {
        desc = route_descriptor{
            /*path_template=*/std::string_view{mr->matched_path_template},
            /*methods=*/hrm->get_allowed_methods(),
            /*is_prefix=*/mr->matched_is_prefix};
    }
    before_handler_ctx ctx{
        /*request=*/mr->dhr.get(),
        /*matched=*/std::move(desc),
        /*method=*/mr->method_enum,
        /*resource=*/hrm.get()};
    if (server_gate) {
        if (auto sc = fire_before_handler(ctx)) {
            mr->response.emplace(std::move(*sc));
            return true;
        }
    }
    if (per_route_gate) {
        if (auto sc = rtable->fire_before_handler(ctx,
                [this](std::string_view m) {
                    log_dispatch_error(std::string(m));
                })) {
            mr->response.emplace(std::move(*sc));
            return true;
        }
    }
    return false;
}

// TASK-050: gated fire of after_handler. Fires between
// dispatch_resource_handler (which populates mr->response) and
// materialize_and_queue_response. A hook returning respond_with(...)
// REPLACES mr->response; the new response then flows through the
// normal materialise + queue path. A hook returning pass() may have
// mutated *mr->response in place via ctx.response->with_header(...)
// (the lvalue & overload).
//
// Per §4.10: after_handler fires only when a handler conceptually ran.
// The pre-handler short-circuit branch (mr->skip_handler) is handled
// upstream in finalize_answer and never reaches this site.
void webserver_impl::fire_after_handler_gated(detail::modded_request* mr,
                                              http_resource* resource) {
    const bool server_gate = has_hooks_for(hook_phase::after_handler);
    // resource is borrowed from finalize_answer's live shared_ptr; no
    // weak_ptr lock() needed (this fires within finalize_answer's scope).
    // nullptr on the 404 path -- no per-route table then.
    auto* rtable = resource != nullptr ? resource->hook_table_raw_() : nullptr;
    const bool route_gate = rtable != nullptr &&
        rtable->any_hooks(hook_phase::after_handler);

    if (!server_gate && !route_gate) return;
    if (!mr->response) return;  // defensive: never fire without a response

    after_handler_ctx ctx{mr->dhr.get(), &*mr->response};
    if (server_gate) {
        if (auto sc = fire_after_handler(ctx)) {
            // Short-circuit: REPLACE mr->response (DR-010 -- emplace
            // destroys the old response, releasing any deferred captures
            // here rather than later in ~modded_request). Per-route
            // chain ALSO sees the replaced response, so refresh ctx.
            mr->response.emplace(std::move(*sc));
            ctx.response = &*mr->response;
        }
    }
    // TASK-051: per-route chain AFTER the server-wide chain. Same
    // short-circuit semantics (respond_with replaces the response).
    if (route_gate) {
        if (auto sc = rtable->fire_after_handler(ctx,
                [this](std::string_view m) {
                    log_dispatch_error(std::string(m));
                })) {
            mr->response.emplace(std::move(*sc));
        }
    }
}

// TASK-050: gated fire of response_sent. Fires immediately AFTER
// MHD_queue_response and BEFORE MHD_destroy_response so the
// ctx.response pointer is still backed by live storage in
// mr->response (which lives until ~modded_request in
// webserver_impl::request_completed). bytes_queued is the logical body
// size from http_response::body_->size(); for deferred/pipe bodies this
// is 0 and consumers should fall back to the Content-Length header.
void webserver_impl::fire_response_sent_gated(detail::modded_request* mr,
                                              http_resource* resource) {
    const bool server_gate = has_hooks_for(hook_phase::response_sent);
    // resource is borrowed from finalize_answer's live shared_ptr (this
    // fires from materialize_and_queue_response, still within that scope),
    // so read the per-route table directly instead of locking the
    // weak_ptr. nullptr on the skip_handler / 404 paths.
    auto* rtable = resource != nullptr ? resource->hook_table_raw_() : nullptr;
    const bool route_gate = rtable != nullptr &&
        rtable->any_hooks(hook_phase::response_sent);

    if (!server_gate && !route_gate && !log_access_alias_) return;
    // mr->response_ is null only if materialize_and_queue_response's
    // belt-and-suspenders fallback also failed; fire nothing rather than crash.
    if (!mr->response) return;

    // 0 for deferred/pipe bodies -- see response_sent_ctx docs.
    const std::size_t bytes_queued = (mr->response->body_ != nullptr)
        ? mr->response->body_->size() : 0;
    // elapsed is consumed by user hooks (server-wide + per-route), not
    // by the log_access alias (which does not read ctx.elapsed).
    // Reuse gate: skip the steady_clock::now() call when only the alias
    // slot fires, to avoid a gratuitous clock syscall on the common path.
    // NOTE: the alias body MUST NOT read ctx.elapsed; any future change
    // that needs elapsed in the alias must also remove this optimisation.
    const auto elapsed = (server_gate || route_gate)
        ? std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - mr->start_time)
        : std::chrono::nanoseconds::zero();
    response_sent_ctx ctx{mr->dhr.get(), &*mr->response,
        mr->response->get_status(), bytes_queued, elapsed};
    fire_response_sent(ctx);
    // TASK-051: per-route chain AFTER server-wide + its alias slot.
    // response_sent is observation-only; no short-circuit logic.
    if (route_gate) {
        rtable->fire_response_sent(ctx,
            [this](std::string_view m) { log_dispatch_error(std::string(m)); });
    }
}

// TASK-050: gated fire of request_completed. Fires from
// webserver_impl::request_completed BEFORE the modded_request is
// deleted so the ctx pointers remain backed by live storage.
//
// `succeeded` maps from MHD_RequestTerminationCode per the ctx docs:
//   MHD_REQUEST_TERMINATED_COMPLETED_OK -> true
//   everything else                     -> false
//
// `resp` is nullable -- on very early MHD failures mr->response may
// never have been populated.
void webserver_impl::fire_request_completed_gated(
        detail::modded_request* mr,
        enum MHD_RequestTerminationCode toe) {
    const bool server_gate = has_hooks_for(hook_phase::request_completed);

    // TASK-051: per-route gate. Unlike after_handler/response_sent, this
    // fires from the MHD completion callback -- finalize_answer's owning
    // shared_ptr is gone, so we must lock() the weak_ptr to keep the
    // resource alive while rtable is in use. per_route_table() returns
    // null if the resource was unregistered between dispatch and
    // completion (action-item contract: skip the per-route chain).
    //
    // But skip the lock() entirely on the common path: mr->route_has_hook_table_
    // is a snapshot (taken in finalize_answer) of whether the resource
    // carried any per-route hook table. When it's false and no server-wide
    // request_completed hook is registered, no control-block atomics are
    // touched. When it's true we still lock() and run the precise
    // any_hooks(request_completed) check, preserving the skip contract.
    std::shared_ptr<http_resource> res;
    resource_hook_table* rtable = nullptr;
    if (server_gate || mr->route_has_hook_table_) {
        rtable = per_route_table(mr, res);
    }
    const bool per_route_present = rtable != nullptr &&
        rtable->any_hooks(hook_phase::request_completed);

    if (!server_gate && !per_route_present) {
        return;
    }
    const http_response* resp_ptr =
        mr->response ? &*mr->response : nullptr;
    // mr->start_time may be epoch (default-constructed) if
    // answer_to_connection never ran (e.g., a port scan: uri_log created
    // mr but MHD closed the connection before dispatching). In that case
    // the subtraction produces a duration in the billions of nanoseconds.
    // Emit nanoseconds{-1} as a sentinel so hook authors can distinguish
    // the degenerate case from a real (but very slow) request.
    const auto raw_duration =
        std::chrono::steady_clock::now() - mr->start_time;
    const auto duration =
        (mr->start_time == std::chrono::steady_clock::time_point{})
            ? std::chrono::nanoseconds{-1}
            : std::chrono::duration_cast<std::chrono::nanoseconds>(
                  raw_duration);
    request_completed_ctx ctx{
        /*request=*/mr->dhr.get(),
        /*resp=*/resp_ptr,
        /*succeeded=*/(toe == MHD_REQUEST_TERMINATED_COMPLETED_OK),
        /*duration=*/duration,
    };
    if (server_gate) {
        fire_request_completed(ctx);
    }
    // TASK-051: per-route chain fires AFTER the server-wide chain.
    if (per_route_present) {
        rtable->fire_request_completed(ctx,
            [this](std::string_view m) { log_dispatch_error(std::string(m)); });
    }
}

}  // namespace detail
}  // namespace httpserver
