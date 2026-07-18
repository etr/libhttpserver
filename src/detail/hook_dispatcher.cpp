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

// hook_dispatcher behavior service (DR-014 §4.11). Logic moved verbatim
// out of the former hook_phase_dispatch.cpp (the eleven forwarders) and
// webserver_hook_firing.cpp (the four gated helpers). Those TUs now hold
// only the thin webserver_impl forwarders during the migration. The one
// behavioral change is that the per-call error logger now routes through
// the detail::log_dispatch_error free function (config bag) instead of the
// webserver_impl member.

#include "httpserver/detail/hook_dispatcher.hpp"

#include <microhttpd.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/detail/body.hpp"
#include "httpserver/detail/dispatch_util.hpp"
#include "httpserver/detail/hook_bus.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/resource_hook_table.hpp"

namespace httpserver {
namespace detail {

bool hook_dispatcher::has_hooks_for(hook_phase p) const noexcept {
    return hooks_.has_hooks_for(p);
}

bool hook_dispatcher::has_handler_exception_alias() const noexcept {
    return hooks_.has_handler_exception_alias();
}

namespace {

// Fetch the per-route hook table (if any) from the request's
// resource_weak_ slot, keeping the shared_ptr alive in res_out so the
// caller can hold the table pointer valid. Returns nullptr when no
// per-route table exists. Uses a direct lock() (atomic) rather than
// expired()+lock() to avoid a TOCTOU window.
resource_hook_table* per_route_table(detail::modded_request* mr,
                                     std::shared_ptr<http_resource>& res_out) {
    res_out = mr->resource_weak_.lock();
    if (!res_out) return nullptr;
    return res_out->hook_table_raw_();
}

}  // namespace

// ---- eleven per-phase forwarders -----------------------------------------

void hook_dispatcher::fire_connection_opened(
    const connection_open_ctx& ctx) noexcept {
    hooks_.fire_connection_opened(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

void hook_dispatcher::fire_accept_decision(const accept_ctx& ctx) noexcept {
    hooks_.fire_accept_decision(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

void hook_dispatcher::fire_connection_closed(
    const connection_close_ctx& ctx) noexcept {
    hooks_.fire_connection_closed(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

std::optional<http_response> hook_dispatcher::fire_request_received(
    request_received_ctx& ctx) noexcept {
    return hooks_.fire_request_received(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

std::optional<http_response> hook_dispatcher::fire_body_chunk(
    body_chunk_ctx& ctx) noexcept {
    return hooks_.fire_body_chunk(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

void hook_dispatcher::fire_route_resolved(
    const route_resolved_ctx& ctx) noexcept {
    hooks_.fire_route_resolved(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

std::optional<http_response> hook_dispatcher::fire_before_handler(
    before_handler_ctx& ctx) noexcept {
    return hooks_.fire_before_handler(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

std::optional<http_response> hook_dispatcher::fire_handler_exception(
    const handler_exception_ctx& ctx) noexcept {
    return hooks_.fire_handler_exception(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

std::optional<http_response> hook_dispatcher::fire_after_handler(
    after_handler_ctx& ctx) noexcept {
    return hooks_.fire_after_handler(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

void hook_dispatcher::fire_response_sent(
    const response_sent_ctx& ctx) noexcept {
    hooks_.fire_response_sent(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

void hook_dispatcher::fire_request_completed(
    const request_completed_ctx& ctx) noexcept {
    hooks_.fire_request_completed(ctx,
        [this](std::string_view m) { log_dispatch_error(config_, m); });
}

// ---- gated: before_handler (server-wide + per-route, short-circuiting) ---

bool hook_dispatcher::fire_before_handler_gated(
        detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    const bool server_gate = hooks_.has_hooks_for(hook_phase::before_handler);
    // rtable comes from hrm directly (already resolved under the route
    // table lock); hrm keeps the resource alive for this function. The
    // acquire barrier from that shared_lock lets hook_table_raw_() observe
    // any prior add_hook() store.
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
        /*request=*/mr->request.get(),
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
                    log_dispatch_error(config_, m);
                })) {
            mr->response.emplace(std::move(*sc));
            return true;
        }
    }
    return false;
}

// ---- gated: after_handler (server-wide + per-route, replace-response) ----

void hook_dispatcher::fire_after_handler_gated(detail::modded_request* mr,
                                               http_resource* resource) {
    const bool server_gate = hooks_.has_hooks_for(hook_phase::after_handler);
    // resource is borrowed from finalize_answer's live shared_ptr; no
    // weak_ptr lock() needed. nullptr on the 404 path -- no per-route table.
    auto* rtable = resource != nullptr ? resource->hook_table_raw_() : nullptr;
    const bool route_gate = rtable != nullptr &&
        rtable->any_hooks(hook_phase::after_handler);

    if (!server_gate && !route_gate) return;
    if (!mr->response) return;  // defensive: never fire without a response

    after_handler_ctx ctx{mr->request.get(), &*mr->response};
    if (server_gate) {
        if (auto sc = fire_after_handler(ctx)) {
            // Short-circuit: REPLACE mr->response (emplace destroys the old
            // response, releasing deferred captures now). The per-route
            // chain ALSO sees the replaced response, so refresh ctx.
            mr->response.emplace(std::move(*sc));
            ctx.response = &*mr->response;
        }
    }
    if (route_gate) {
        if (auto sc = rtable->fire_after_handler(ctx,
                [this](std::string_view m) {
                    log_dispatch_error(config_, m);
                })) {
            mr->response.emplace(std::move(*sc));
        }
    }
}

// ---- gated: response_sent (observation, server-wide + per-route) ---------

void hook_dispatcher::fire_response_sent_gated(detail::modded_request* mr,
                                               http_resource* resource) {
    const bool server_gate = hooks_.has_hooks_for(hook_phase::response_sent);
    // resource is borrowed from finalize_answer's live shared_ptr, so read
    // the per-route table directly instead of locking the weak_ptr. nullptr
    // on the skip_handler / 404 paths.
    auto* rtable = resource != nullptr ? resource->hook_table_raw_() : nullptr;
    const bool route_gate = rtable != nullptr &&
        rtable->any_hooks(hook_phase::response_sent);
    // Whether any user hook (server-wide or per-route) will observe this
    // firing. Distinct from the log_access alias slot, which fires
    // regardless but never reads ctx.elapsed (see the elapsed gate below).
    const bool user_gate = server_gate || route_gate;

    if (!user_gate && !hooks_.has_log_access_alias()) return;
    // mr->response is null only if materialize_and_queue_response's
    // belt-and-suspenders fallback also failed; fire nothing rather than crash.
    if (!mr->response) return;

    // 0 for deferred/pipe bodies -- see response_sent_ctx docs.
    const std::size_t bytes_queued = (mr->response->body_ != nullptr)
        ? mr->response->body_->size() : 0;
    // elapsed is consumed by user hooks, not by the log_access alias.
    // Skip the steady_clock::now() syscall when only the alias slot fires.
    // NOTE: the alias body MUST NOT read ctx.elapsed; any future change that
    // needs elapsed in the alias must also remove this optimisation.
    const auto elapsed = user_gate
        ? std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - mr->start_time)
        : std::chrono::nanoseconds::zero();
    response_sent_ctx ctx{mr->request.get(), &*mr->response,
        mr->response->get_status(), bytes_queued, elapsed};
    fire_response_sent(ctx);
    // Per-route chain AFTER server-wide + its alias slot.
    // response_sent is observation-only; no short-circuit logic.
    if (route_gate) {
        rtable->fire_response_sent(ctx,
            [this](std::string_view m) { log_dispatch_error(config_, m); });
    }
}

// ---- gated: request_completed (fires from MHD completion callback) -------

void hook_dispatcher::fire_request_completed_gated(
        detail::modded_request* mr,
        enum MHD_RequestTerminationCode toe) {
    const bool server_gate =
        hooks_.has_hooks_for(hook_phase::request_completed);

    // Per-route gate. This fires from the MHD completion callback --
    // finalize_answer's owning shared_ptr is gone -- so lock() the weak_ptr
    // to keep the resource alive while rtable is in use. Skip the lock()
    // entirely on the common path: route_has_hook_table_ is a snapshot of
    // whether the resource carried any per-route hook table; when it's false
    // and no server-wide hook is registered, no control-block atomics are
    // touched.
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
    // mr->start_time may be epoch if answer_to_connection never ran (e.g. a
    // port scan: uri_log created mr but MHD closed before dispatch). Emit
    // nanoseconds{-1} as a sentinel so hook authors can distinguish the
    // degenerate case from a real (but very slow) request.
    const auto raw_duration =
        std::chrono::steady_clock::now() - mr->start_time;
    const auto duration =
        (mr->start_time == std::chrono::steady_clock::time_point{})
            ? std::chrono::nanoseconds{-1}
            : std::chrono::duration_cast<std::chrono::nanoseconds>(
                  raw_duration);
    request_completed_ctx ctx{
        /*request=*/mr->request.get(),
        /*resp=*/resp_ptr,
        /*succeeded=*/(toe == MHD_REQUEST_TERMINATED_COMPLETED_OK),
        /*duration=*/duration,
    };
    if (server_gate) {
        fire_request_completed(ctx);
    }
    if (per_route_present) {
        rtable->fire_request_completed(ctx,
            [this](std::string_view m) { log_dispatch_error(config_, m); });
    }
}

}  // namespace detail
}  // namespace httpserver
