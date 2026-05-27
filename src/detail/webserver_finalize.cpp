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

// TASK-050: hook firing helpers + ctx builders for the three tail-end
// lifecycle phases (after_handler, response_sent, request_completed).
//
// Carved out of webserver_request.cpp + webserver_callbacks.cpp to keep
// each host TU under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). Mirrors the TASK-046 carve-out
// (webserver_callbacks_lifecycle.cpp) and TASK-047
// (webserver_body_pipeline.cpp).
//
// Each helper performs the gated-fire dance:
//   - Read the relaxed any_hooks_[phase] atomic; early-return if false.
//   - For the alias-only slots (log_access_alias_): also check the slot
//     pointer before short-circuiting.
//   - Aggregate-init the ctx struct.
//   - Call the corresponding fire_X helper on webserver_impl.
//   - On respond_with short-circuit (after_handler only): emplace the
//     new response into mr->response_, replacing the in-flight one
//     (DR-010: emplace destroys the old http_response so any deferred
//     captures are released here, not later in ~modded_request).

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
resource_hook_table* per_route_table(detail::modded_request* mr,
                                     std::shared_ptr<http_resource>& res_out) {
    if (mr->resource_weak_.expired()) return nullptr;
    res_out = mr->resource_weak_.lock();
    if (!res_out) return nullptr;
    return res_out->hook_table_raw_();
}

}  // namespace

// TASK-051: gated fire of before_handler, server-wide AND per-route,
// with short-circuit detection. Carved out of finalize_answer to keep
// the host function under CCN_MAX after TASK-051's per-route extension
// roughly doubled the local branch count. Returns true iff either chain
// short-circuited (mr->response_ has already been emplaced; caller must
// route straight to materialize_and_queue_response). False means both
// chains passed (or both gates were closed) and dispatch should proceed.
bool webserver_impl::fire_before_handler_gated(
        detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    const bool server_gate =
        any_hooks_[static_cast<std::size_t>(hook_phase::before_handler)]
            .load(std::memory_order_relaxed);
    auto* per_route_table = hrm->hook_table_raw_();
    const bool per_route_gate = per_route_table != nullptr &&
        per_route_table->any_hooks(hook_phase::before_handler);
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
            mr->response_.emplace(std::move(*sc));
            return true;
        }
    }
    if (per_route_gate) {
        if (auto sc = per_route_table->fire_before_handler(ctx,
                [this](std::string_view m) {
                    log_dispatch_error(std::string(m));
                })) {
            mr->response_.emplace(std::move(*sc));
            return true;
        }
    }
    return false;
}

// TASK-050: gated fire of after_handler. Fires between
// dispatch_resource_handler (which populates mr->response_) and
// materialize_and_queue_response. A hook returning respond_with(...)
// REPLACES mr->response_; the new response then flows through the
// normal materialise + queue path. A hook returning pass() may have
// mutated *mr->response_ in place via ctx.response->with_header(...)
// (the lvalue & overload).
//
// Per §4.10: after_handler fires only when a handler conceptually ran.
// The pre-handler short-circuit branch (mr->skip_handler) is handled
// upstream in finalize_answer and never reaches this site.
void webserver_impl::fire_after_handler_gated(detail::modded_request* mr) {
    const bool server_gate =
        any_hooks_[static_cast<std::size_t>(hook_phase::after_handler)]
            .load(std::memory_order_relaxed);
    std::shared_ptr<http_resource> res;
    auto* rtable = per_route_table(mr, res);
    const bool route_gate = rtable != nullptr &&
        rtable->any_hooks(hook_phase::after_handler);

    if (!server_gate && !route_gate) return;
    if (!mr->response_) return;  // defensive: never fire without a response

    after_handler_ctx ctx{mr->dhr.get(), &*mr->response_};
    if (server_gate) {
        if (auto sc = fire_after_handler(ctx)) {
            // Short-circuit: REPLACE mr->response_ (DR-010 -- emplace
            // destroys the old response, releasing any deferred captures
            // here rather than later in ~modded_request). Per-route
            // chain ALSO sees the replaced response, so refresh ctx.
            mr->response_.emplace(std::move(*sc));
            ctx.response = &*mr->response_;
        }
    }
    // TASK-051: per-route chain AFTER the server-wide chain. Same
    // short-circuit semantics (respond_with replaces the response).
    if (route_gate) {
        if (auto sc = rtable->fire_after_handler(ctx,
                [this](std::string_view m) {
                    log_dispatch_error(std::string(m));
                })) {
            mr->response_.emplace(std::move(*sc));
        }
    }
}

// TASK-050: gated fire of response_sent. Fires immediately AFTER
// MHD_queue_response and BEFORE MHD_destroy_response so the
// ctx.response pointer is still backed by live storage in
// mr->response_ (which lives until ~modded_request in
// webserver_impl::request_completed). bytes_queued is the logical body
// size from http_response::body_->size(); for deferred/pipe bodies this
// is 0 and consumers should fall back to the Content-Length header.
void webserver_impl::fire_response_sent_gated(detail::modded_request* mr) {
    const bool server_gate =
        any_hooks_[static_cast<std::size_t>(hook_phase::response_sent)]
            .load(std::memory_order_relaxed);
    std::shared_ptr<http_resource> res;
    auto* rtable = per_route_table(mr, res);
    const bool route_gate = rtable != nullptr &&
        rtable->any_hooks(hook_phase::response_sent);

    if (!server_gate && !route_gate && !log_access_alias_) return;
    if (!mr->response_) return;

    const std::size_t bytes = (mr->response_->body_ != nullptr)
        ? mr->response_->body_->size() : 0;
    // elapsed is consumed by user hooks (server-wide + per-route), not
    // by the log_access alias. Skip the steady_clock::now() call when
    // only the alias slot is going to fire.
    const auto elapsed = (server_gate || route_gate)
        ? std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - mr->start_time)
        : std::chrono::nanoseconds::zero();
    response_sent_ctx ctx{mr->dhr.get(), &*mr->response_,
        mr->response_->get_status(), bytes, elapsed};
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
// `resp` is nullable -- on very early MHD failures mr->response_ may
// never have been populated.
void webserver_impl::fire_request_completed_gated(
        detail::modded_request* mr,
        enum MHD_RequestTerminationCode toe) {
    const bool server_gate =
        any_hooks_[static_cast<std::size_t>(hook_phase::request_completed)]
            .load(std::memory_order_relaxed);

    // TASK-051: per-route gate. lock() may return null if the resource
    // was unregistered between dispatch and completion (per the action
    // item contract: skip the per-route chain in that case).
    std::shared_ptr<http_resource> res;
    detail::resource_hook_table* rtable = nullptr;
    if (!mr->resource_weak_.expired()) {
        res = mr->resource_weak_.lock();
        if (res) {
            rtable = res->hook_table_raw_();
        }
    }
    const bool per_route_present = rtable != nullptr &&
        rtable->any_hooks(hook_phase::request_completed);

    if (!server_gate && !per_route_present) {
        return;
    }
    const http_response* resp_ptr =
        mr->response_ ? &*mr->response_ : nullptr;
    // mr->start_time may be epoch if answer_to_connection never ran
    // (uri_log created mr but the connection failed before
    // answer_to_connection). In that case `duration` is meaninglessly
    // large; documented in the ctx field doc -- no observable test
    // failure because request_completed without a response is a degenerate
    // path that consumers rarely care about.
    request_completed_ctx ctx{
        /*request=*/mr->dhr.get(),
        /*resp=*/resp_ptr,
        /*succeeded=*/(toe == MHD_REQUEST_TERMINATED_COMPLETED_OK),
        /*duration=*/std::chrono::steady_clock::now() - mr->start_time,
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
