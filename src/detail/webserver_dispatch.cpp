/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINDOWS
#else
#if defined(__CYGWIN__)
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <errno.h>
#include <microhttpd.h>
#ifdef HAVE_WEBSOCKET
#include <microhttpd_ws.h>
#endif  // HAVE_WEBSOCKET
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <algorithm>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/method_utils.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/resource_hook_table.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/body.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif  // HAVE_GNUTLS

using std::string;
using std::map;

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

// Error-page helpers (not_found_page, method_not_allowed_page,
// internal_error_page, log_dispatch_error, run_internal_error_handler_safely)
// live in detail/webserver_error_pages.cpp to keep this TU under the
// 500-LOC ceiling (FILE_LOC_MAX in scripts/check-file-size.sh).
//
// The route-table state + lookup pipeline (lookup_v2, the 3-tier walk,
// canonicalize_lookup_path, invalidate_route_cache) live in the
// route_table collaborator (src/detail/route_table.cpp). This TU reaches
// them through webserver_impl's thin forwarders (routes_.lookup_v2 etc.).

// ----------------------------------------------------------------------
// v2 lookup-backed resolver.
// ----------------------------------------------------------------------

// Routes finalize_answer through
// lookup_v2 (the 3-tier v2 table fronted by route_lru_cache). See
// header comment in webserver_impl_dispatch.hpp for the full contract.
//
// The dispatch path:
//   lookup_v2() walks route_lru_cache -> exact_routes_ ->
//       param_and_prefix_routes_ -> regex_routes_, returning a
//       route_entry whose handler is a shared_ptr<http_resource>
//       (the on_*/route entry points wrap user lambdas in lambda_resource
//       before storing — see prepare_or_create_lambda_shim in
//       webserver_routes.cpp).
//
// single_resource servers register their handler via register_prefix("")
// or register_prefix("/") -- both surface as a radix-tier prefix
// terminus at "/", which lookup_v2 finds on the radix-tier walk. There
// is no separate fast path because the v2 walk for a single registered
// prefix is one shared_lock + one empty-map probe + one trivial radix
// descent: cheap enough that the extra branch was net-negative.
//
// route_entry::handler is a plain shared_ptr<http_resource> (every
// writer wraps lambdas in lambda_resource before storing). The
// dispatch path reads the shared_ptr directly with a defensive null
// check that degrades to a 404 miss.
bool webserver_impl::resolve_resource_for_request(detail::modded_request* mr,
        std::shared_ptr<http_resource>& hrm) {
    // matched_path_template + matched_is_prefix feed the route_resolved
    // and before_handler hook ctxs. Skip the heap allocation when no
    // hook in either phase is registered.
    const bool need_path_template =
        has_hooks_for(hook_phase::route_resolved) ||
        has_hooks_for(hook_phase::before_handler);

    // v2 lookup pipeline: cache -> exact -> radix -> regex.
    lookup_result result = lookup_v2(mr->method_enum, mr->standardized_url);
    if (!result.found) return false;

    // Read the resource pointer directly. Every writer of route_entry
    // populates a non-null shared_ptr (a class-derived http_resource
    // for register_path / register_prefix, or a lambda_resource shim
    // for on_* / route); a null pointer here would indicate a future
    // bug, so degrade to a not-found miss defensively.
    if (result.entry.handler == nullptr) {
        return false;
    }
    hrm = result.entry.handler;

    // Replay captured URL parameters into the request. Per-name
    // set_arg matches v1 behaviour exactly — duplicates with later
    // wins, etc.
    if (mr->request != nullptr) {
        for (const auto& [name, value] : result.captured_params) {
            mr->request->set_arg(name, value);
        }
    }

    // Populate the hook ctx scratch slots when at least one hook is
    // registered for the phases that read them. v2 cache_value does
    // not store the matched URL template; fall back to standardized_url
    // (matches the v1 cache-hit path the legacy resolver used).
    if (need_path_template) {
        mr->matched_path_template = mr->standardized_url;
        mr->matched_is_prefix = result.entry.is_prefix;
    }
    return true;
}

std::string webserver_impl::serialize_allow_methods(method_set allowed) const {
    // Thin wrapper over detail::format_allow_header (method_utils.hpp).
    // Production dispatch calls hrm->get_allow_header() directly; this
    // member survives only as a test seam (bench_warm_path.cpp,
    // webserver_on_methods_test.cpp).
    return detail::format_allow_header(allowed);
}

namespace {

// Shared body of the two dispatch_resource_handler catch arms.
// Either routes the dispatch-thrown exception through the
// handler_exception hook chain (when any user hooks are registered or
// the internal_error_handler alias slot is wired) or falls back to the
// v1 run_internal_error_handler_safely path. Extracted to keep
// dispatch_resource_handler under the per-function CCN bar.
void handle_dispatch_exception(
        webserver_impl* impl,
        detail::modded_request* mr,
        std::string_view message) {
    // Per-route handler_exception: the weak_ptr was set on mr in
    // finalize_answer before dispatch_resource_handler was called.
    // res keeps the resource alive while rtable is in use (the shared_ptr
    // must not go out of scope before the rtable firing loop finishes).
    auto res = mr->resource_weak_.lock();
    auto* rtable = res ? res->hook_table_raw_() : nullptr;
    const bool per_route = rtable != nullptr &&
        rtable->any_hooks(hook_phase::handler_exception);
    const bool server_chain =
        impl->has_hooks_for(hook_phase::handler_exception) ||
        impl->hooks_.has_handler_exception_alias();

    if (server_chain || per_route) {
        // Capture the live exception_ptr before constructing the ctx so
        // the side-effectful call is separated from the struct literal.
        // (Naming the capture makes clear we are taking a reference to
        // the in-flight exception object.)
        auto current_exc = std::current_exception();
        handler_exception_ctx ctx{mr->request.get(), current_exc, message};
        if (server_chain) {
            if (auto sc = impl->fire_handler_exception(ctx)) {
                mr->response.emplace(std::move(*sc));
                return;
            }
        }
        if (per_route) {
            // Per-route chain runs AFTER server-wide. Same
            // semantics: respond_with() short-circuits the chain.
            if (auto sc = rtable->fire_handler_exception(ctx,
                    [impl](std::string_view m) {
                        impl->log_dispatch_error(std::string(m));
                    })) {
                mr->response.emplace(std::move(*sc));
                return;
            }
        }
        // Every hook (and the alias) ran without a
        // response -- emit the hardcoded empty-body 500 directly.
        mr->response.emplace(
            impl->internal_error_page(mr, "", /*force_our=*/true));
        return;
    }
    // Backwards-compat fast path: no hook chain at all. This path
    // (no hooks, no alias, handler throws) is covered by the integ
    // tests in test/integ/basic.cpp (response_throws_runtime_error and
    // response_throws_non_std_exception test groups).
    mr->response.emplace(
        impl->run_internal_error_handler_safely(mr, message));
}

}  // namespace

void webserver_impl::dispatch_resource_handler(detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    try {
        if (mr->pp != nullptr) {
            MHD_destroy_post_processor(mr->pp);
            mr->pp = nullptr;
        }
        // before_handler fires from finalize_answer so auth and
        // method-not-allowed alias hooks run as part of the unified
        // before_handler chain before dispatch_resource_handler is called.
        // dispatch_resource_handler is only reached when the hook chain
        // passes through without short-circuiting.
        //
        // The is_allowed check below remains as the default (no-hook)
        // fallback for servers that have not registered a
        // method_not_allowed_handler alias hook. When a functional alias
        // hook is registered it fires and short-circuits before reaching
        // here, so this check only runs when method_not_allowed_handler
        // is not set (zero-cost-when-unused for the alias hook).
        if (hrm->is_allowed(mr->method_enum)) {
            // Pointer-to-member dispatch returns http_response
            // by value; the prvalue is moved into the
            // per-connection optional anchor. shared_ptr has no
            // operator->*, so call as ((*ptr).*pmf)(...).
            mr->response.emplace(((*hrm).*(mr->callback))(*mr->request));
            if (mr->response->get_status() == -1) {
                // No exception was thrown, but the handler
                // returned the default-sentinel response. Route through
                // the safe internal-error path so a misbehaving user
                // handler can't escape into libmicrohttpd. (The "null
                // response" arm from v1 is now structurally impossible:
                // the optional always holds a value at this point.)
                mr->response.emplace(run_internal_error_handler_safely(
                    mr, "handler returned null response"));
            }
            return;
        }
        // Method not allowed: emit the Allow header.
        // Read the resource's lazily-cached Allow header value instead
        // of rebuilding the string from the mask on every 405.  The
        // cache lives on http_resource (the same object that owns the
        // mask), regenerates implicitly when the mask differs from the
        // last-cached snapshot, and is thread-safe under a per-resource
        // mutex held only across the cache-fill path.
        mr->response.emplace(method_not_allowed_page(mr));
        const std::string& header_value = hrm->get_allow_header();
        if (!header_value.empty()) {
            mr->response->with_header(http_utils::http_header_allow, header_value);
        }
    } catch (const std::exception& e) {
        // Handler threw std::exception. The exception is routed
        // through the handler_exception
        // hook chain (with the internal_error_handler alias as the
        // last-position fallback) inside handle_dispatch_exception.
        //
        // Perf: only build the heap string
        // when a log_error callback is actually wired; log_dispatch_error
        // also checks this but the concatenation happens at the call site.
        if (parent->config.log_error) {
            log_dispatch_error(
                std::string("dispatch: handler threw std::exception: ")
                    .append(e.what()));
        }
        handle_dispatch_exception(this, mr, std::string_view{e.what()});
    } catch (...) {
        // Handler threw non-std::exception. Same flow as
        // the std::exception arm but with the sentinel message.
        log_dispatch_error("dispatch: handler threw unknown exception");
        handle_dispatch_exception(this, mr,
                                  std::string_view{"unknown exception"});
    }
}


}  // namespace detail

}  // namespace httpserver
