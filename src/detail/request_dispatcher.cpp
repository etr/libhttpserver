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

// request_dispatcher behavior service (DR-014 §4.11). finalize_answer +
// resolve_resource_for_request moved verbatim out of
// detail/webserver_request.cpp; dispatch_resource_handler +
// handle_dispatch_exception out of detail/webserver_dispatch.cpp; the
// file-local fire_route_resolved_gated moved here too. Rewiring vs the
// originals: lookup goes through routes_ (route_table), all hook firing +
// gating through hooks_ (hook_dispatcher), 404/405/500 through errors_
// (error_pages), queueing through materializer_ (response_materializer),
// the websocket probe through ws_upgrader_ (HAVE_WEBSOCKET), and
// log_dispatch_error is the free function over config_.

#include "httpserver/detail/request_dispatcher.hpp"

#include <microhttpd.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "httpserver/create_webserver.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/dispatch_util.hpp"
#include "httpserver/detail/error_pages.hpp"
#include "httpserver/detail/hook_dispatcher.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/resource_hook_table.hpp"
#include "httpserver/detail/response_materializer.hpp"
#include "httpserver/detail/route_table.hpp"
#ifdef HAVE_WEBSOCKET
#include "httpserver/detail/websocket_upgrader.hpp"
#endif  // HAVE_WEBSOCKET

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

bool request_dispatcher::resolve_resource_for_request(detail::modded_request* mr,
        std::shared_ptr<http_resource>& hrm) {
    // matched_path_template + matched_is_prefix feed the route_resolved and
    // before_handler hook ctxs. Skip the heap allocation when no hook in
    // either phase is registered.
    const bool need_path_template =
        hooks_.has_hooks_for(hook_phase::route_resolved) ||
        hooks_.has_hooks_for(hook_phase::before_handler);

    // v2 lookup pipeline: cache -> exact -> radix -> regex.
    route_table::lookup_result result =
        routes_.lookup_v2(mr->method_enum, mr->standardized_url);
    if (!result.found) return false;

    // Every writer of route_entry populates a non-null shared_ptr; a null
    // pointer here would indicate a future bug, so degrade to a not-found
    // miss defensively.
    if (result.entry.handler == nullptr) {
        return false;
    }
    hrm = result.entry.handler;

    // Replay captured URL parameters into the request (per-name set_arg
    // matches v1 behaviour: duplicates with later wins).
    if (mr->request != nullptr) {
        for (const auto& [name, value] : result.captured_params) {
            mr->request->set_arg(name, value);
        }
    }

    // Populate the hook ctx scratch slots when at least one hook is
    // registered for the phases that read them. v2 does not store the
    // matched URL template; fall back to standardized_url.
    if (need_path_template) {
        mr->matched_path_template = mr->standardized_url;
        mr->matched_is_prefix = result.entry.is_prefix;
    }
    return true;
}

namespace {

// Shared body of the two dispatch_resource_handler catch arms. Either
// routes the dispatch-thrown exception through the handler_exception hook
// chain (when any user hooks are registered or the internal_error_handler
// alias slot is wired) or falls back to run_internal_error_handler_safely.
// Extracted to keep dispatch_resource_handler under the CCN bar.
void handle_dispatch_exception(hook_dispatcher& hooks, error_pages& errors,
        const webserver_config& config, detail::modded_request* mr,
        std::string_view message) {
    // Per-route handler_exception: the weak_ptr was set on mr in
    // finalize_answer before dispatch_resource_handler was called. res keeps
    // the resource alive while rtable is in use.
    auto res = mr->resource_weak_.lock();
    auto* rtable = res ? res->hook_table_raw_() : nullptr;
    const bool per_route = rtable != nullptr &&
        rtable->any_hooks(hook_phase::handler_exception);
    const bool server_chain =
        hooks.has_hooks_for(hook_phase::handler_exception) ||
        hooks.has_handler_exception_alias();

    if (server_chain || per_route) {
        // Capture the live exception_ptr before constructing the ctx so the
        // side-effectful call is separated from the struct literal.
        auto current_exc = std::current_exception();
        handler_exception_ctx ctx{mr->request.get(), current_exc, message};
        if (server_chain) {
            if (auto sc = hooks.fire_handler_exception(ctx)) {
                mr->response.emplace(std::move(*sc));
                return;
            }
        }
        if (per_route) {
            // Per-route chain runs AFTER server-wide. Same semantics:
            // respond_with() short-circuits the chain.
            if (auto sc = rtable->fire_handler_exception(ctx,
                    [&config](std::string_view m) {
                        log_dispatch_error(config, m);
                    })) {
                mr->response.emplace(std::move(*sc));
                return;
            }
        }
        // Every hook (and the alias) ran without a response -- emit the
        // hardcoded empty-body 500 directly.
        mr->response.emplace(
            errors.internal_error_page(mr, "", /*force_our=*/true));
        return;
    }
    // Backwards-compat fast path: no hook chain at all.
    mr->response.emplace(
        errors.run_internal_error_handler_safely(mr, message));
}

}  // namespace

void request_dispatcher::dispatch_resource_handler(detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    try {
        if (mr->pp != nullptr) {
            MHD_destroy_post_processor(mr->pp);
            mr->pp = nullptr;
        }
        // before_handler fires from finalize_answer, so auth and
        // method-not-allowed alias hooks run as part of the unified
        // before_handler chain before this is called; the is_allowed check
        // below is the default (no-hook) 405 fallback.
        if (hrm->is_allowed(mr->method_enum)) {
            // Pointer-to-member dispatch returns http_response by value; the
            // prvalue is moved into the per-connection optional anchor.
            mr->response.emplace(((*hrm).*(mr->callback))(*mr->request));
            if (mr->response->get_status() == -1) {
                // Handler returned the default-sentinel response. Route
                // through the safe internal-error path.
                mr->response.emplace(errors_.run_internal_error_handler_safely(
                    mr, "handler returned null response"));
            }
            return;
        }
        // Method not allowed: emit the Allow header from the resource's
        // lazily-cached value.
        mr->response.emplace(errors_.method_not_allowed_page(mr));
        const std::string& header_value = hrm->get_allow_header();
        if (!header_value.empty()) {
            mr->response->with_header(http_utils::http_header_allow, header_value);
        }
    } catch (const std::exception& e) {
        // Handler threw std::exception -> handler_exception chain (with the
        // internal_error_handler alias as last-position fallback). Only build
        // the heap string when a log_error callback is wired.
        if (config_.log_error) {
            log_dispatch_error(config_,
                std::string("dispatch: handler threw std::exception: ")
                    .append(e.what()));
        }
        handle_dispatch_exception(hooks_, errors_, config_, mr,
                                  std::string_view{e.what()});
    } catch (...) {
        // Handler threw non-std::exception. Same flow, sentinel message.
        log_dispatch_error(config_, "dispatch: handler threw unknown exception");
        handle_dispatch_exception(hooks_, errors_, config_, mr,
                                  std::string_view{"unknown exception"});
    }
}

namespace {

void fire_route_resolved_gated(hook_dispatcher& hooks,
                               detail::modded_request* mr, bool found,
                               const std::shared_ptr<http_resource>& hrm) {
    if (!hooks.has_hooks_for(hook_phase::route_resolved)) {
        return;
    }
    std::optional<route_descriptor> desc;
    if (found && hrm) {
        desc = route_descriptor{
            /*path_template=*/std::string_view{mr->matched_path_template},
            /*methods=*/hrm->get_allowed_methods(),
            /*is_prefix=*/mr->matched_is_prefix};
    }
    route_resolved_ctx ctx{
        /*request=*/mr->request.get(),
        /*matched=*/std::move(desc),
        /*resource=*/hrm ? hrm.get() : nullptr};
    hooks.fire_route_resolved(ctx);
}

}  // namespace

std::optional<MHD_Result> request_dispatcher::try_ws_upgrade(
        MHD_Connection* connection, detail::modded_request* mr) {
#ifdef HAVE_WEBSOCKET
    return ws_upgrader_.try_handle(connection, mr);
#else
    (void)connection;
    (void)mr;
    return std::nullopt;
#endif  // HAVE_WEBSOCKET
}

MHD_Result request_dispatcher::finalize_answer(MHD_Connection* connection,
        detail::modded_request* mr) {
    if (auto ws_result = try_ws_upgrade(connection, mr)) {
        return *ws_result;
    }

    // A pre-handler short-circuit hook (request_received or body_chunk)
    // already populated mr->response. Skip route lookup, auth, and dispatch
    // -- go straight to the response queue. after_handler is NOT fired on
    // this path (no handler ran); response_sent fires unconditionally in
    // materialize_and_queue_response.
    if (mr->skip_handler) {
        return materializer_.materialize_and_queue_response(connection, mr,
                                                             nullptr);
    }

    // Hold a shared_ptr copy across dispatch so a concurrent
    // unregister_resource cannot free the resource mid-call.
    std::shared_ptr<http_resource> hrm;
    bool found = resolve_resource_for_request(mr, hrm);

    if (found) {
        // Snapshot whether this resource carries a per-route hook table so
        // fire_request_completed_gated (fires after this shared_ptr is gone)
        // can gate its weak_ptr lock() on the common zero-per-route-hook path.
        mr->route_has_hook_table_ = (hrm->hook_table_raw_() != nullptr);

        // Only stamp the weak_ptr when a later out-of-scope consumer can use
        // it (the MHD completion callback), i.e. iff this resource has a
        // per-route hook table OR a server-wide request_completed hook is
        // registered. Gating this drops 2 control-block atomics per matched
        // request on the common zero-hook path.
        if (mr->route_has_hook_table_ ||
                hooks_.has_hooks_for(hook_phase::request_completed)) {
            mr->resource_weak_ = hrm;
        }
    }

    fire_route_resolved_gated(hooks_, mr, found, hrm);

    // Fire before_handler from here (not inside dispatch_resource_handler) so
    // auth + method-not-allowed alias hooks run as part of the unified
    // before_handler chain (per-route firing included by the gate). If either
    // chain short-circuited, mr->response is already populated -> materialise.
    if (found && hooks_.fire_before_handler_gated(mr, hrm)) {
        return materializer_.materialize_and_queue_response(connection, mr,
                                                             hrm.get());
    }

    if (found) {
        dispatch_resource_handler(mr, hrm);
    } else if (!mr->response) {
        mr->response.emplace(errors_.not_found_page(mr));
    }

    // after_handler fires between handler return (or 404 synthesis) and
    // materialise. hrm is null on the 404 path; both the gate and the
    // materialiser tolerate a null resource.
    hooks_.fire_after_handler_gated(mr, hrm.get());

    return materializer_.materialize_and_queue_response(connection, mr,
                                                        hrm.get());
}

}  // namespace detail
}  // namespace httpserver
