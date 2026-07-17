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

void webserver_impl::invalidate_route_cache() {
    // Called by registration callers after any table mutation.
    // Contract: caller must NOT hold route_table_mutex_ here -- the lock is
    // taken internally by route_cache::clear() (route_cache.hpp), and
    // holding route_table_mutex_ across this call is unnecessary and risks
    // a lock-ordering inversion with the 3-tier lookup path below.
    route_lru_cache.clear();
}

// 3-tier route lookup pipeline (lookup_v2, defined below):
//   1. exact tier — transparent-map probe under a shared route-table
//      lock. Deliberately bypasses the LRU cache; the rationale lives
//      at the probe site inside lookup_v2.
//   2. parameter/regex LRU cache (cache mutex only) — return on hit,
//      promoting LRU.
//   3. on miss, take a shared_lock on route_table_mutex_:
//      a. param_and_prefix_routes_ (segment-trie)
//      b. regex_routes_ (linear scan)
//      then drop the table lock and install the result into the cache
//      (we never hold both locks simultaneously — the table lock is
//      released before the cache lock is taken).
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
//
// Returns a string_view so the happy path (input
// already canonical) allocates zero heap memory.  On the rewrite
// path the canonicalised form is written into the caller-owned
// @p scratch buffer and a view into that buffer is returned.
//
// LIFETIME: the returned view is valid for the duration of the
// call chain only; it points at either the immutable "/" literal,
// the caller's @p path argument, or the caller's @p scratch buffer.
// Any caller storing the view must copy it into an owning string
// first.  The cache layer (cache_key constructed below) already
// copies via std::string, so the contract is naturally respected.
static std::string_view canonicalize_lookup_path(
        std::string_view path, std::string& scratch) {
    if (path.empty()) {
        // Immutable canonical root -- no scratch usage.
        return std::string_view{"/", 1};
    }
    const bool has_leading_slash = (path.front() == '/');
    const bool has_trailing_slash = (path.size() > 1 && path.back() == '/');
    if (has_leading_slash && !has_trailing_slash) {
        // Already canonical: return the caller's view directly.
        return path;
    }
    // Rewrite path: write the canonicalised form into the caller's
    // scratch buffer and return a view into it.
    scratch.clear();
    scratch.reserve(path.size() + (has_leading_slash ? 0 : 1));
    if (!has_leading_slash) {
        scratch.push_back('/');
    }
    if (has_trailing_slash) {
        scratch.append(path.data(), path.size() - 1);
    } else {
        scratch.append(path.data(), path.size());
    }
    return std::string_view{scratch};
}

webserver_impl::lookup_result
webserver_impl::lookup_v2(http_method method, const std::string& path) {
    lookup_result result;

    // canonicalize_lookup_path returns a string_view
    // into either @p path (already-canonical happy path -- no
    // allocation), the immutable "/" literal (empty-input case), or
    // @p canonicalize_scratch (rewrite case -- single allocation,
    // bounded by the input size).
    std::string canonicalize_scratch;
    std::string_view lookup_path =
        canonicalize_lookup_path(path, canonicalize_scratch);

    // Step 1: exact tier, probed FIRST and under the route-table shared
    // lock only. exact_routes_ uses std::less<> (transparent), so the
    // string_view key needs no std::string allocation, and concurrent
    // worker threads read it in parallel with zero route_lru_cache
    // traffic.
    //
    // The exact tier deliberately BYPASSES route_lru_cache: fronting an
    // O(log n) transparent map probe with the cache would put every
    // request behind the cache's exclusive std::mutex plus an LRU splice
    // WRITE on each hit -- serialising the hottest dispatch path and
    // dirtying a shared cache line across the thread pool -- for no
    // lookup-cost saving. Only the parameter/regex tiers, whose match is
    // genuinely expensive, are cached below. This matches the v1 dispatch
    // model, where exact routes were a plain shared_lock map probe and
    // only regex results were memoised.
    {
        std::shared_lock table_lock(route_table_mutex_);
        auto exact_it = exact_routes_.find(lookup_path);
        if (exact_it != exact_routes_.end()) {
            result.found = true;
            result.tier = tier_hit::exact;
            result.entry = exact_it->second;
            // exact tier carries no parameters by definition.
            return result;
        }
    }

    // Step 2: parameter/regex cache. Cache under the canonical key so
    // /foo and /foo/ share an entry. find_by_view avoids copying
    // lookup_path into a cache_key on the warm path.
    cache_value cached;
    if (route_lru_cache.find_by_view(method, lookup_path, cached)) {
        result.found = true;
        result.tier = tier_hit::cache;
        result.entry = std::move(cached.entry);
        result.captured_params = std::move(cached.captured_params);
        return result;
    }

    // Step 3: cache miss -- walk the parameter/prefix (radix) then regex
    // tiers under the shared lock. Construct the owning cache_key once
    // here; the exact-hit and warm-cache paths never reach this line.
    cache_key key{method, std::string(lookup_path)};
    {
        std::shared_lock table_lock(route_table_mutex_);

        // Radix tier — segment-trie walk.
        radix_match<route_entry> rm;
        if (param_and_prefix_routes_.find(key.path, rm) && rm.entry) {
            result.found = true;
            result.tier = tier_hit::radix;
            result.entry = *rm.entry;
            result.captured_params = std::move(rm.captures);
        }

        // Regex tier — linear scan over pre-compiled std::regex objects.
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

    // Step 4: install radix/regex results into the cache. Copy (not
    // move) — the caller consumes `result` after this returns, and a
    // move would leave the shared_ptr variant arm null (false-negative
    // 404).
    if (result.found) {
        route_lru_cache.insert(
            key, cache_value{result.entry, result.captured_params});
    }

    return result;
}

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
        impl->handler_exception_alias_;

    if (server_chain || per_route) {
        // Capture the live exception_ptr before constructing the ctx so
        // the side-effectful call is separated from the struct literal.
        // (Naming the capture makes clear we are taking a reference to
        // the in-flight exception object.)
        auto current_exc = std::current_exception();
        handler_exception_ctx ctx{mr->dhr.get(), current_exc, message};
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
            mr->response.emplace(((*hrm).*(mr->callback))(*mr->dhr));
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
        if (parent->log_error) {
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
