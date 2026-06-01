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

// TASK-048: error-page helpers (not_found_page, method_not_allowed_page,
// internal_error_page, log_dispatch_error, run_internal_error_handler_safely)
// moved to detail/webserver_error_pages.cpp to keep this TU under the
// 500-LOC ceiling (FILE_LOC_MAX in scripts/check-file-size.sh) once the
// route_resolved and before_handler firing sites landed.

void webserver_impl::invalidate_route_cache() {
    // Clear the v2 LRU cache. (The v1 dispatch cache — route_cache_list
    // / route_cache_map / route_cache_mutex_ — was removed in TASK-053
    // step 3 along with the v1 resolver.)
    route_lru_cache.clear();
}

// TASK-027: 3-tier route lookup. Pipeline:
//   1. cache lookup (cache mutex only) — return on hit, promoting LRU.
//   2. on miss, take a shared_lock on route_table_mutex_:
//      a. exact_routes_ (hash, O(1))
//      b. param_and_prefix_routes_ (segment-trie)
//      c. regex_routes_ (linear scan)
//   3. on hit at any tier, drop the table lock and install into the
//      cache (lock acquisition order: table BEFORE cache; we never hold
//      both simultaneously — the table lock is released before the cache
//      lock is taken).
//
// The method-set check (does the entry serve `method`?) lives at the
// dispatch site, NOT here, because the existing 405 + Allow: header
// path needs to see the entry even when no method bit matches.
// Canonicalize a lookup path the same way http_endpoint canonicalizes a
// registration path: strip a trailing '/' (unless the path IS just "/"),
// prepend '/' if missing. Registration stores keys under url_complete,
// which is produced by this same normalization (see http_endpoint.cpp
// ll. 60-67) — so lookup must canonicalize too or "/foo" and "/foo/"
// would never share an entry. Matches the v1 dispatch path, which
// constructs a non-registration http_endpoint at lookup time and so
// gets the same normalization for free.
static std::string canonicalize_lookup_path(const std::string& path) {
    if (path.empty()) {
        return "/";
    }
    std::string out = path;
    if (out.front() != '/') {
        out.insert(out.begin(), '/');
    }
    if (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

webserver_impl::lookup_result
webserver_impl::lookup_v2(http_method method, const std::string& path) {
    lookup_result result;

    std::string lookup_path = canonicalize_lookup_path(path);

    // Step 1: cache. Cache under the canonical key so /foo and /foo/
    // share an entry. Use find_by_view to avoid copying lookup_path
    // into a cache_key on every call, including warm-cache hits.
    // cache_key is only constructed when an insert is needed (miss path).
    cache_value cached;
    if (route_lru_cache.find_by_view(method, lookup_path, cached)) {
        result.found = true;
        result.tier = tier_hit::cache;
        result.entry = std::move(cached.entry);
        result.captured_params = std::move(cached.captured_params);
        return result;
    }
    // Construct key by moving lookup_path into the cache_key, avoiding a
    // second heap allocation. All subsequent tier lookups use key.path,
    // which is the same std::string now owned by the key.
    cache_key key{method, std::move(lookup_path)};

    // Step 2: walk the tiers under a shared lock.
    {
        std::shared_lock table_lock(route_table_mutex_);

        // 2a. Exact tier — single hash probe.
        auto exact_it = exact_routes_.find(key.path);
        if (exact_it != exact_routes_.end()) {
            result.found = true;
            result.tier = tier_hit::exact;
            result.entry = exact_it->second;
            // exact tier carries no parameters by definition.
        }

        // 2b. Radix tier — segment-trie walk.
        if (!result.found) {
            radix_match<route_entry> rm;
            if (param_and_prefix_routes_.find(key.path, rm) && rm.entry) {
                result.found = true;
                result.tier = tier_hit::radix;
                result.entry = *rm.entry;
                result.captured_params = std::move(rm.captures);
            }
        }

        // 2c. Regex tier — linear scan over pre-compiled std::regex objects.
        // Patterns were compiled once at registration time (in register_impl_
        // and on_methods_), so no compilation cost is paid per lookup.
        if (!result.found) {
            for (const auto& rr : regex_routes_) {
                if (std::regex_match(key.path, rr.compiled_re)) {
                    result.found = true;
                    result.tier = tier_hit::regex;
                    result.entry = rr.entry;
                    break;
                }
            }
        }
    }  // table_lock released.

    // Step 3: install into cache (cache mutex only). Copy result.entry and
    // result.captured_params into the cache_value — the caller (the v2
    // resolver) consumes `result` after this returns, so a move-out of
    // `result.entry` here would leave the caller's `std::get_if` reading a
    // moved-from variant (the shared_ptr arm is null after a move, causing
    // a false-negative 404). The copy is one shared_ptr ref-count bump and
    // one std::vector copy on the miss path — cache hits are unaffected.
    if (result.found) {
        cache_value v;
        v.entry = result.entry;
        v.captured_params = result.captured_params;
        route_lru_cache.insert(key, std::move(v));
    }

    return result;
}

}  // namespace detail

namespace detail {

// TASK-053: v2 lookup-backed resolver. Routes finalize_answer through
// lookup_v2 (the 3-tier v2 table fronted by route_lru_cache) — the v1
// resolver and its helpers (lookup_route_cache, scan_regex_routes,
// store_route_cache, apply_extracted_params) were deleted in TASK-053
// step 3. See header comment in webserver_impl_dispatch.hpp for the
// full contract.
//
// The dispatch path now does:
//   lookup_v2() walks route_lru_cache -> exact_routes_ ->
//       param_and_prefix_routes_ -> regex_routes_, returning a
//       route_entry whose handler arm is always shared_ptr<http_resource>
//       (the on_*/route entry points wrap user lambdas in lambda_resource
//       before storing; the variant's lambda_handler arm is dead code).
//
// The parent->single_resource fast path is intentionally preserved
// here: it reads the single registered endpoint directly from
// registered_resources rather than falling through to lookup_v2 (which
// would also work because single_resource installs a radix prefix at
// "/"). Reading registered_resources avoids the captured-params /
// route_entry plumbing for a configuration that has no parameters.
bool webserver_impl::resolve_resource_for_request(detail::modded_request* mr,
        std::shared_ptr<http_resource>& hrm) {
    // matched_path_template + matched_is_prefix feed the route_resolved
    // and before_handler hook ctxs. Skip the heap allocation when no
    // hook in either phase is registered.
    const bool need_path_template =
        has_hooks_for(hook_phase::route_resolved) ||
        has_hooks_for(hook_phase::before_handler);

    // single_resource fast path: the one registered endpoint serves
    // every URL. Read it directly from registered_resources.
    // registered_resources_mutex protects a registration-time data
    // store; under dispatch we still need a shared lock to make the
    // read-then-copy atomic with respect to a concurrent
    // unregister_path. (NOTE: single_resource webservers do not
    // support runtime register/unregister in practice, but the lock
    // is cheap when uncontended and the safety guarantee is worth it.)
    if (parent->single_resource) {
        std::shared_lock registered_resources_lock(registered_resources_mutex);
        if (registered_resources.empty()) return false;
        const auto& only = *registered_resources.begin();
        hrm = only.second;
        if (need_path_template) {
            mr->matched_path_template = only.first.get_url_complete();
            mr->matched_is_prefix = only.first.is_family_url();
        }
        return true;
    }

    // v2 lookup pipeline: cache -> exact -> radix -> regex.
    lookup_result result = lookup_v2(mr->method_enum, mr->standardized_url);
    if (!result.found) return false;

    // Extract the shared_ptr<http_resource> arm of the route_entry's
    // variant. The on_* / route entry points wrap user lambdas in a
    // lambda_resource shim and store the shim as shared_ptr<http_resource>,
    // so this arm is the only one populated in practice; treat the
    // lambda_handler arm as unreachable (defensive nullptr check below).
    auto* hp = std::get_if<std::shared_ptr<http_resource>>(&result.entry.handler);
    if (hp == nullptr || *hp == nullptr) {
        // Unreachable today: lookup_v2 only returns entries whose
        // handler is a shared_ptr<http_resource>. If a future task
        // stores a lambda_handler directly we will need to grow this
        // path; until then, treat as a not-found miss.
        return false;
    }
    hrm = *hp;

    // Replay captured URL parameters into the request. This is the v2
    // equivalent of the v1-era apply_extracted_params (removed in
    // TASK-053 step 3). Per-name set_arg matches v1 behaviour exactly —
    // duplicates with later wins, etc.
    if (mr->dhr != nullptr) {
        for (const auto& [name, value] : result.captured_params) {
            mr->dhr->set_arg(name, value);
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
    // TASK-048 review cleanup (findings 3 & 4): delegate to the shared free
    // function format_allow_header in detail/method_utils.hpp.  The member
    // function is retained so existing call sites (dispatch_resource_handler
    // below) compile unchanged; internally it is now a thin wrapper.
    return detail::format_allow_header(allowed);
}

namespace {

// TASK-049: shared body of the two dispatch_resource_handler catch arms.
// Either routes the dispatch-thrown exception through the
// handler_exception hook chain (when any user hooks are registered or
// the internal_error_handler alias slot is wired) or falls back to the
// v1 run_internal_error_handler_safely path. Extracted to keep
// dispatch_resource_handler under the per-function CCN bar.
void handle_dispatch_exception(
        webserver_impl* impl,
        detail::modded_request* mr,
        std::string_view message) {
    // TASK-051: per-route handler_exception. weak_ptr was set on mr in
    // finalize_answer before dispatch_resource_handler was called.
    // res keeps the resource alive while rtable is in use (the shared_ptr
    // must not go out of scope before the rtable firing loop finishes).
    auto res = mr->resource_weak_.lock();
    auto* rtable = res ? res->hook_table_raw_() : nullptr;
    const bool per_route = rtable != nullptr &&
        rtable->any_hooks(hook_phase::handler_exception);
    const bool server_chain =
        impl->has_hooks_for(hook_phase::handler_exception) ||
        impl->handler_exception_alias_;

    if (server_chain || per_route) {
        // Capture the live exception_ptr before constructing the ctx so
        // the side-effectful call is separated from the struct literal.
        // (Finding #24 / code-simplifier: naming the capture makes clear
        // we are taking a reference to the in-flight exception object.)
        auto current_exc = std::current_exception();
        handler_exception_ctx ctx{mr->dhr.get(), current_exc, message};
        if (server_chain) {
            if (auto sc = impl->fire_handler_exception(ctx)) {
                mr->response.emplace(std::move(*sc));
                return;
            }
        }
        if (per_route) {
            // Per-route chain runs AFTER server-wide. Same DR-009 §5.2
            // semantics: respond_with() short-circuits the chain.
            if (auto sc = rtable->fire_handler_exception(ctx,
                    [impl](std::string_view m) {
                        impl->log_dispatch_error(std::string(m));
                    })) {
                mr->response.emplace(std::move(*sc));
                return;
            }
        }
        // §5.2 point 4: every hook (and the alias) ran without a
        // response -- emit the hardcoded empty-body 500 directly.
        mr->response.emplace(
            impl->internal_error_page(mr, "", /*force_our=*/true));
        return;
    }
    // Backwards-compat fast path: no hook chain at all.
    // Finding #40 (test-quality-reviewer): this path (no hooks, no alias,
    // handler throws) is covered by the pre-existing integ tests in
    // test/integ/basic.cpp (response_throws_runtime_error and
    // response_throws_non_std_exception test groups; see basic.cpp around
    // the "AC2" acceptance-criteria block). No new test is needed here.
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
        // TASK-048: before_handler firing moved to finalize_answer so auth
        // and method-not-allowed alias hooks run as part of the unified
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
            // TASK-036: pointer-to-member dispatch returns http_response
            // by value (DR-004); the prvalue is moved into the
            // per-connection optional anchor. shared_ptr has no
            // operator->*, so call as ((*ptr).*pmf)(...).
            mr->response.emplace(((*hrm).*(mr->callback))(*mr->dhr));
            if (mr->response->get_status() == -1) {
                // TASK-031: no exception was thrown, but the handler
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
        // Method not allowed: serialize the allow-mask into a header.
        mr->response.emplace(method_not_allowed_page(mr));
        std::string header_value = serialize_allow_methods(hrm->get_allowed_methods());
        if (!header_value.empty()) {
            mr->response->with_header(http_utils::http_header_allow, header_value);
        }
    } catch (const std::exception& e) {
        // TASK-031 / DR-009 §5.2 point 2: handler threw std::exception.
        // TASK-049 routes the exception through the handler_exception
        // hook chain (with the internal_error_handler alias as the
        // last-position fallback) inside handle_dispatch_exception.
        //
        // Finding #29 (performance-reviewer): only build the heap string
        // when a log_error callback is actually wired; log_dispatch_error
        // also checks this but the concatenation happens at the call site.
        if (parent->log_error) {
            log_dispatch_error(
                std::string("dispatch: handler threw std::exception: ")
                    .append(e.what()));
        }
        handle_dispatch_exception(this, mr, std::string_view{e.what()});
    } catch (...) {
        // §5.2 point 3: handler threw non-std::exception. Same flow as
        // the std::exception arm but with the sentinel message.
        log_dispatch_error("dispatch: handler threw unknown exception");
        handle_dispatch_exception(this, mr,
                                  std::string_view{"unknown exception"});
    }
}


}  // namespace detail

}  // namespace httpserver
