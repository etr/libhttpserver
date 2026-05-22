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
#include "httpserver/detail/modded_request.hpp"
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

namespace httpserver {

namespace detail {

http_response webserver_impl::not_found_page(detail::modded_request* mr) const {
    if (parent->not_found_handler != nullptr) {
        return parent->not_found_handler(*mr->dhr);
    }
    return http_response::string(std::string{constants::NOT_FOUND_ERROR})
        .with_status(http_utils::http_not_found);
}

http_response webserver_impl::method_not_allowed_page(detail::modded_request* mr) const {
    if (parent->method_not_allowed_handler != nullptr) {
        return parent->method_not_allowed_handler(*mr->dhr);
    }
    return http_response::string(std::string{constants::METHOD_ERROR})
        .with_status(http_utils::http_method_not_allowed);
}

http_response webserver_impl::internal_error_page(
    detail::modded_request* mr,
    std::string_view msg,
    bool force_our) const {
    // TASK-031 / DR-009 §5.2 point 4: the double-fault fallback. Used when
    // the user-supplied internal_error_handler itself threw or when the
    // belt-and-suspenders site after get_raw_response_with_fallback fires.
    // The body is intentionally empty and the message is intentionally
    // ignored.
    if (force_our) {
        return http_response::empty()
            .with_status(http_utils::http_internal_server_error);
    }
    // §5.2 point 2/3: invoke the user handler with the originating message.
    if (parent->internal_error_handler != nullptr) {
        return parent->internal_error_handler(*mr->dhr, msg);
    }
    // No handler configured: surface the message in the default body so
    // the unset-handler path is informative for debugging. Operators who
    // need a fixed body can wire a constant-returning handler.
    return http_response::string(std::string{msg})
        .with_status(http_utils::http_internal_server_error);
}

void webserver_impl::log_dispatch_error(std::string_view msg) const {
    if (parent->log_error == nullptr) {
        return;
    }
    // A misbehaving user logger must not poison the catch from inside the
    // catch. Swallow any exception it throws; we have no recovery beyond
    // dropping the log line.
    try {
        parent->log_error(std::string(msg));
    } catch (...) {
        // Intentionally suppressed.
    }
}

http_response
webserver_impl::run_internal_error_handler_safely(
    detail::modded_request* mr,
    std::string_view msg) const {
    try {
        return internal_error_page(mr, msg, /*force_our=*/false);
    } catch (...) {
        // §5.2 point 4: the user handler itself threw. Log generically
        // and return an empty-body 500. No exception escapes from here.
        log_dispatch_error("internal_error_handler threw; "
                           "sending hardcoded empty-body 500");
        return internal_error_page(mr, "", /*force_our=*/true);
    }
}

void webserver_impl::invalidate_route_cache() {
    // Clear both the v1 and v2 caches. v1's cache is keyed on
    // standardized_url only; v2's is keyed on (method, path). A
    // registration may invalidate both, so clear both atomically.
    {
        std::lock_guard<std::mutex> lock(route_cache_mutex);
        route_cache_list.clear();
        route_cache_map.clear();
    }
    route_cache_v2.clear();
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

    const std::string lookup_path = canonicalize_lookup_path(path);

    // Step 1: cache. Cache under the canonical key so /foo and /foo/
    // share an entry.
    cache_key key{method, lookup_path};
    cache_value cached;
    if (route_cache_v2.find(key, cached)) {
        result.found = true;
        result.tier = tier_hit::cache;
        result.entry = std::move(cached.entry);
        result.captured_params = std::move(cached.captured_params);
        return result;
    }

    // Step 2: walk the tiers under a shared lock.
    {
        std::shared_lock table_lock(route_table_mutex_);

        // 2a. Exact tier — single hash probe.
        auto exact_it = exact_routes_.find(lookup_path);
        if (exact_it != exact_routes_.end()) {
            result.found = true;
            result.tier = tier_hit::exact;
            result.entry = exact_it->second;
            // exact tier carries no parameters by definition.
        }

        // 2b. Radix tier — segment-trie walk.
        if (!result.found) {
            radix_match<route_entry> rm;
            if (param_and_prefix_routes_.find(lookup_path, rm) && rm.entry) {
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
                if (std::regex_match(lookup_path, rr.compiled_re)) {
                    result.found = true;
                    result.tier = tier_hit::regex;
                    result.entry = rr.entry;
                    break;
                }
            }
        }
    }  // table_lock released.

    // Step 3: install into cache (cache mutex only).
    if (result.found) {
        cache_value v;
        v.entry = result.entry;
        v.captured_params = result.captured_params;
        route_cache_v2.insert(key, std::move(v));
    }

    return result;
}

}  // namespace detail

namespace detail {

std::optional<webserver_impl::regex_route_lookup>
webserver_impl::lookup_route_cache(const std::string& key) {
    std::lock_guard<std::mutex> cache_lock(route_cache_mutex);
    auto cache_it = route_cache_map.find(key);
    if (cache_it == route_cache_map.end()) {
        return std::nullopt;
    }
    // Cache hit — promote to LRU front and copy the match data while
    // still under the cache lock, so a concurrent invalidation can't
    // free the cached endpoint out from under us.
    route_cache_list.splice(route_cache_list.begin(), route_cache_list, cache_it->second);
    const route_cache_entry& cached = cache_it->second->second;
    return regex_route_lookup{
        cached.resource,
        cached.matched_endpoint.get_url_pars(),
        cached.matched_endpoint.get_chunk_positions(),
    };
}

std::optional<webserver_impl::regex_route_scan_hit>
webserver_impl::scan_regex_routes(const detail::http_endpoint& target) {
    // Longest-match-wins tie-breaking: prefer the endpoint with more
    // url-pieces; among equals, prefer the longer url_complete string.
    bool found = false;
    size_t len = 0;
    size_t tot_len = 0;
    map<detail::http_endpoint, std::shared_ptr<http_resource>>::iterator found_endpoint;
    for (auto it = registered_resources_regex.begin();
            it != registered_resources_regex.end(); ++it) {
        size_t endpoint_pieces_len = it->first.get_url_pieces().size();
        size_t endpoint_tot_len = it->first.get_url_complete().size();
        if (found && endpoint_pieces_len <= len
                && !(endpoint_pieces_len == len && endpoint_tot_len > tot_len)) {
            continue;
        }
        if (!it->first.match(target)) continue;
        found = true;
        len = endpoint_pieces_len;
        tot_len = endpoint_tot_len;
        found_endpoint = it;
    }
    if (!found) return std::nullopt;
    return regex_route_scan_hit{found_endpoint->first, found_endpoint->second};
}

void webserver_impl::store_route_cache(const std::string& key,
                                       const detail::http_endpoint& matched,
                                       std::shared_ptr<http_resource> hrm) {
    std::lock_guard<std::mutex> cache_lock(route_cache_mutex);
    route_cache_list.emplace_front(key, route_cache_entry{matched, std::move(hrm)});
    route_cache_map[key] = route_cache_list.begin();
    if (route_cache_map.size() > ROUTE_CACHE_MAX_SIZE) {
        route_cache_map.erase(route_cache_list.back().first);
        route_cache_list.pop_back();
    }
}

void webserver_impl::apply_extracted_params(detail::modded_request* mr,
        const detail::http_endpoint& target,
        const std::vector<std::string>& url_pars,
        const std::vector<int>& chunks) {
    const auto& url_pieces = target.get_url_pieces();
    for (unsigned int i = 0; i < url_pars.size(); i++) {
        if (chunks[i] < 0 || static_cast<size_t>(chunks[i]) >= url_pieces.size()) continue;
        mr->dhr->set_arg(url_pars[i], url_pieces[chunks[i]]);
    }
}

bool webserver_impl::resolve_resource_for_request(detail::modded_request* mr,
        std::shared_ptr<http_resource>& hrm) {
    std::shared_lock registered_resources_lock(registered_resources_mutex);

    if (parent->single_resource) {
        hrm = registered_resources.begin()->second;
        return true;
    }

    auto fe = registered_resources_str.find(mr->standardized_url.c_str());
    if (fe != registered_resources_str.end()) {
        hrm = fe->second;
        return true;
    }

    if (!parent->regex_checking) return false;

    detail::http_endpoint endpoint(mr->standardized_url.c_str(), false, false, false);

    if (auto cached = lookup_route_cache(mr->standardized_url)) {
        hrm = cached->hrm;
        apply_extracted_params(mr, endpoint, cached->url_pars, cached->chunks);
        return true;
    }

    auto scan_hit = scan_regex_routes(endpoint);
    if (!scan_hit) return false;

    hrm = scan_hit->hrm;
    store_route_cache(mr->standardized_url, scan_hit->endpoint, hrm);
    apply_extracted_params(mr, endpoint, scan_hit->endpoint.get_url_pars(),
                           scan_hit->endpoint.get_chunk_positions());
    return true;
}

bool webserver_impl::apply_auth_short_circuit(detail::modded_request* mr) {
    if (parent->auth_handler == nullptr) return false;
    std::string path(mr->dhr->get_path());
    if (should_skip_auth(path)) return false;
    // TASK-036 boundary: auth_handler_ptr is intentionally NOT touched
    // by this task (PRD-AUTH out of scope). It still returns a
    // shared_ptr<http_response>; when set it acts as a poor-man's
    // optional. Move the pointee into the by-value slot so the
    // dispatch path is uniform from here on. The shared_ptr drops at
    // end of scope.
    std::shared_ptr<http_response> auth_response = parent->auth_handler(*mr->dhr);
    if (auth_response == nullptr) return false;
    mr->response_.emplace(std::move(*auth_response));
    return true;
}

std::string webserver_impl::serialize_allow_methods(method_set allowed) const {
    // TASK-021: enum-declaration order (GET, HEAD, POST, PUT, DELETE,
    // CONNECT, OPTIONS, TRACE, PATCH). v1 used std::map iteration order
    // (alphabetical); the only existing assertion in-tree is
    // "HEAD, POST" which is preserved by enum order.
    std::string header_value;
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (!allowed.contains(m)) continue;
        if (!header_value.empty()) header_value += ", ";
        header_value += std::string(to_string(m));
    }
    return header_value;
}

void webserver_impl::dispatch_resource_handler(detail::modded_request* mr,
        const std::shared_ptr<http_resource>& hrm) {
    try {
        if (mr->pp != nullptr) {
            MHD_destroy_post_processor(mr->pp);
            mr->pp = nullptr;
        }
        if (hrm->is_allowed(mr->method_enum)) {
            // TASK-036: pointer-to-member dispatch returns http_response
            // by value (DR-004); the prvalue is moved into the
            // per-connection optional anchor. shared_ptr has no
            // operator->*, so call as ((*ptr).*pmf)(...).
            mr->response_.emplace(((*hrm).*(mr->callback))(*mr->dhr));
            if (mr->response_->get_status() == -1) {
                // TASK-031: no exception was thrown, but the handler
                // returned the default-sentinel response. Route through
                // the safe internal-error path so a misbehaving user
                // handler can't escape into libmicrohttpd. (The "null
                // response" arm from v1 is now structurally impossible:
                // the optional always holds a value at this point.)
                mr->response_.emplace(run_internal_error_handler_safely(
                    mr, "handler returned null response"));
            }
            return;
        }
        // Method not allowed: serialize the allow-mask into a header.
        mr->response_.emplace(method_not_allowed_page(mr));
        std::string header_value = serialize_allow_methods(hrm->get_allowed_methods());
        if (!header_value.empty()) {
            mr->response_->with_header(http_utils::http_header_allow, header_value);
        }
    } catch (const std::exception& e) {
        // TASK-031 / DR-009 §5.2 point 2: handler threw std::exception.
        // Log via error_logger, forward e.what() to internal_error_handler.
        // run_internal_error_handler_safely contains a possible re-throw
        // from the user handler (point 4).
        log_dispatch_error(std::string("dispatch: handler threw "
                                       "std::exception: ") + e.what());
        mr->response_.emplace(run_internal_error_handler_safely(mr, e.what()));
    } catch (...) {
        // §5.2 point 3: handler threw non-std::exception. Same flow as
        // the std::exception arm but with the sentinel message.
        log_dispatch_error("dispatch: handler threw unknown exception");
        mr->response_.emplace(run_internal_error_handler_safely(mr, "unknown exception"));
    }
}


}  // namespace detail

}  // namespace httpserver
