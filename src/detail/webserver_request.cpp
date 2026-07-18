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
#include <chrono>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
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
#include "httpserver/detail/path_normalize.hpp"
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
using std::pair;
using std::vector;
using std::map;
using std::set;

namespace httpserver {

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;


namespace detail {

namespace {

// NOTE: the caller (should_skip_auth) must receive an already-unescaped
// path (i.e., no %XX sequences remain). libhttpserver's base_unescaper()
// (called in answer_to_connection) runs before should_skip_auth, so this
// invariant is satisfied on the dispatch path. Double slashes (//) and
// trailing slashes are collapsed automatically: empty segments between
// consecutive '/' separators are skipped, producing the same result as a
// single '/'.
//
// Path-normalization chain (per request, in order):
//   1. http_utils::standardize_url (answer_to_connection) collapses
//      duplicate '/' runs and strips a trailing '/'.
//   2. normalize_path (below), applied to the standardized URL,
//      resolves dot-segments ("." / "..") into a canonical absolute
//      path; the result is stored as mr->standardized_url and is the
//      single path the rest of dispatch sees.
//   3. canonicalize_lookup_path, inside lookup_v2
//      (webserver_dispatch.cpp), canonicalizes slashes on the lookup
//      key (leading '/' ensured, trailing '/' stripped) so lookups hit
//      the same keys registration stored.
// should_skip_auth re-runs normalize_path on its input (idempotent on
// the already-normalized dispatch path), so the auth-skip decision and
// the route lookup always agree on the same canonical path. A past
// auth-bypass fix (dot-segment mismatch between auth and routing,
// commit a3e53f3) depends on this agreement -- do not let the two
// views diverge.
//
// Single pass, no per-segment heap allocation: each retained segment is
// appended straight into the output buffer, and a stack of segment start
// offsets lets ".." pop the previous segment by truncating the buffer
// back to that offset. This runs on every request (the auth-bypass
// canonicalisation, commit a3e53f3), so it avoids the vector<std::string>
// of owning segments the earlier tokenize-and-rebuild form allocated.
std::string normalize_path(std::string_view path) {
    std::string out;
    out.reserve(path.size() + 1);
    out.push_back('/');
    // Offsets into `out` where each retained segment begins, recorded
    // just BEFORE its leading separator so ".." can drop the whole "/seg"
    // by resizing back to the recorded offset.
    std::vector<std::string::size_type> seg_marks;
    std::string::size_type start = 0;
    if (!path.empty() && path[0] == '/') start = 1;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string_view seg = path.substr(start, end - start);
        start = end + 1;
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!seg_marks.empty()) {
                out.resize(seg_marks.back());
                seg_marks.pop_back();
            }
            continue;
        }
        seg_marks.push_back(out.size());
        if (out.size() > 1) out.push_back('/');
        out.append(seg.data(), seg.size());
    }
    return out;
}

}  // namespace

// Pre-normalize each auth_skip_paths entry once at
// webserver construction time.  Entries ending in "/*" keep their
// wildcard suffix; the prefix before the wildcard is normalized.
// Callers (webserver::webserver) pass the raw config-bag list and
// store the result on the webserver instance as a sibling to the
// original `auth_skip_paths` list.  Without this pre-normalization
// the skip list would be matched verbatim against a normalized
// request path, so non-canonical entries (e.g. "/public/",
// "/a/../b") would silently never match.
//
// Entries containing '%' are rejected with
// std::invalid_argument.  Skip-path entries must be provided in
// decoded form (the same form as the request path after
// libhttpserver's base_unescaper() runs).  A '%'-encoded entry would
// never match a decoded request path and would silently bypass auth
// for no route -- a misconfiguration hazard caught early here.
std::vector<std::string> normalize_auth_skip_paths(
        const std::vector<std::string>& raw) {
    std::vector<std::string> out;
    out.reserve(raw.size());
    for (const auto& entry : raw) {
        // Reject percent-encoded entries: skip-path entries must be
        // provided in decoded form.  A '%' in the entry indicates a
        // URL-encoded sequence that would never match the decoded
        // request path produced by libhttpserver's base_unescaper().
        if (entry.find('%') != std::string::npos) {
            throw std::invalid_argument(
                "auth_skip_paths entry contains a percent-encoded "
                "sequence ('" + entry + "'). "
                "Skip-path entries must be provided in decoded form "
                "(e.g. '/public/test', not '/public%2Ftest').");
        }
        // Wildcard suffix: strip the trailing "/*", normalize the
        // prefix, then re-append "/*".  The special case "/*" (size
        // == 2) means "match every path" and is stored as-is so
        // should_skip_auth can recognise it with the >= 2 guard.
        if (entry.size() >= 2 && entry.back() == '*' &&
            entry[entry.size() - 2] == '/') {
            if (entry.size() == 2) {
                // "/*" -- global wildcard: matches every path.
                out.push_back("/*");
            } else {
                std::string prefix = entry.substr(0, entry.size() - 2);
                std::string normalized_prefix = normalize_path(prefix);
                if (normalized_prefix == "/") {
                    // Prefix collapsed to root -- treat as "/*".
                    out.push_back("/*");
                } else {
                    out.push_back(normalized_prefix + "/*");
                }
            }
            continue;
        }
        out.push_back(normalize_path(entry));
    }
    return out;
}

bool webserver_impl::should_skip_auth(std::string_view path) const {
    // Empty-list early-out.  Servers with no
    // auth_skip_paths configured pay zero normalization cost.  This
    // is the production-typical case for any server whose auth
    // surface either covers every route or has no auth_handler at
    // all.
    if (parent->auth_skip_paths_normalized.empty()) {
        return false;
    }

    // Compare against the pre-normalized list (built
    // once at construction time) instead of re-normalizing skip-list
    // entries on every request.  The per-request normalize_path call
    // on @p path remains -- the inbound URL is per-request data and
    // cannot be pre-normalized.
    std::string normalized = normalize_path(path);

    for (const auto& skip_path : parent->auth_skip_paths_normalized) {
        if (skip_path == normalized) return true;
        // Support wildcard suffix (e.g., "/public/*").
        // Use >= 2 (not > 2) so the global
        // wildcard "/*" (size == 2) is handled.  When skip_path is "/*"
        // the prefix is "/" and every normalized path starts with "/",
        // so we return true immediately for any request.
        if (skip_path.size() >= 2 && skip_path.back() == '*' &&
            skip_path[skip_path.size() - 2] == '/') {
            std::string_view prefix(skip_path.data(), skip_path.size() - 1);
            if (normalized.compare(0, prefix.size(), prefix.data(),
                                   prefix.size()) == 0) {
                return true;
            }
        }
    }
    return false;
}

// requests_answer_first_step and requests_answer_second_step
// live in detail/webserver_body_pipeline.cpp to keep this TU under the
// 500-LOC ceiling (FILE_LOC_MAX in scripts/check-file-size.sh).

// finalize_answer, resolve_resource_for_request, dispatch_resource_handler,
// and the fire_route_resolved_gated helper moved to the request_dispatcher
// behavior service (detail/request_dispatcher.cpp, DR-014 §4.11).
// complete_request (the request_pipeline's terminal step) hands off to it
// via impl_->dispatcher_.

MHD_Result webserver_impl::complete_request(MHD_Connection* connection, struct detail::modded_request* mr, const char* version, const char* method) {
    // mr->ws is pre-populated in answer_to_connection (hoisted there for
    // early-path request_completed coverage); no need to set it again here.
    mr->request->set_path(mr->standardized_url);
    mr->request->set_method(method);
    mr->request->set_version(version);

    return dispatcher_.finalize_answer(connection, mr);
}

void webserver_impl::resolve_method_callback(const char* method,
                                              detail::modded_request* mr) {
    // Case-sensitive per RFC 7230 §3.1.1: HTTP method is case-sensitive.
    // Also record the enum form once so finalize_answer can call
    // hrm->is_allowed without re-scanning the wire string.
    // Unrecognised methods leave mr->method_enum at the default
    // (count_), so is_allowed(count_) returns false and the request
    // takes the 405 path. mr->callback is left at nullptr (its
    // default-initializer value) for unrecognised methods; the 405 guard
    // in dispatch_resource_handler fires before it is ever invoked.
    //
    // Data-driven lookup table: a new HTTP method requires only one
    // row here (wire string, callback pointer, enum value, has_body
    // flag).
    using render_fn = http_response (http_resource::*)(const http_request&);
    struct method_entry {
        const char*  wire;
        render_fn    callback;
        http_method  enum_val;
        bool         has_body;
    };
    static const method_entry methods[] = {
        { http_utils::http_method_get,     &http_resource::render_get,     http_method::get,     false },
        { http_utils::http_method_post,    &http_resource::render_post,    http_method::post,    true  },
        { http_utils::http_method_put,     &http_resource::render_put,     http_method::put,     true  },
        { http_utils::http_method_delete,  &http_resource::render_delete,  http_method::del,     true  },
        { http_utils::http_method_patch,   &http_resource::render_patch,   http_method::patch,   true  },
        { http_utils::http_method_head,    &http_resource::render_head,    http_method::head,    false },
        { http_utils::http_method_connect, &http_resource::render_connect, http_method::connect, false },
        { http_utils::http_method_trace,   &http_resource::render_trace,   http_method::trace,   false },
        { http_utils::http_method_options, &http_resource::render_options, http_method::options, false },
    };
    for (const auto& e : methods) {
        if (0 == strcmp(method, e.wire)) {
            mr->callback    = e.callback;
            mr->method_enum = e.enum_val;
            if (e.has_body) mr->has_body = true;
            return;
        }
    }
    // Unrecognised method: leave mr->callback == nullptr and
    // mr->method_enum == http_method::count_ (both set by modded_request
    // default initialiser); the 405 guard fires before callback is used.
}

MHD_Result webserver_impl::answer_to_connection(void* cls, MHD_Connection* connection, const char* url, const char* method,
        const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls) {
    auto* mr = static_cast<detail::modded_request*>(*con_cls);
    auto* impl = static_cast<webserver_impl*>(cls);

    if (mr->request) {
        return impl->requests_answer_second_step(connection, method, version,
                                                  upload_data, upload_data_size, mr);
    }

    const MHD_ConnectionInfo* conninfo =
        MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    if (conninfo != nullptr && impl->parent->config.tcp_nodelay) {
        int yes = 1;
        setsockopt(conninfo->connect_fd, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&yes), sizeof(int));
    }

    // Anchor for response_sent.elapsed and
    // request_completed.duration. Captured here -- the earliest moment
    // for the request inside the dispatch path. uri_log runs earlier
    // but is also invoked on non-HTTP traffic (#371); answer_to_connection
    // is the first point where a real HTTP request is unambiguously
    // in flight.
    mr->start_time = std::chrono::steady_clock::now();
    // Hoist the parent-webserver back-pointer here (rather than in
    // complete_request) so the request_completed firing site
    // can reach impl_->any_hooks_ even on request_received short-circuit
    // paths that may not reach complete_request.
    mr->ws = impl->parent;

    std::string t_url = url;
    base_unescaper(&t_url, impl->parent->config.unescaper);
    // SECURITY: collapse dot-segments ("." / "..") into the canonical
    // path here, at the single point where the routing/auth path is
    // derived. Both the route matcher (segment_trie::find via
    // mr->standardized_url) and should_skip_auth() must interpret the
    // path identically; should_skip_auth() runs the path through
    // normalize_path() (which pops ".."), but standardize_url() only
    // collapses duplicate '/' and a trailing '/'. Without this, a request
    // such as "/admin/../public/x" normalizes to "/public/x" for the
    // auth-skip check (auth skipped) yet the router still descends to the
    // "/admin" prefix/regex handler -- an authentication bypass. Applying
    // normalize_path() to the standardized URL makes the two views agree;
    // it is idempotent w.r.t. the normalize_path() call already in
    // should_skip_auth().
    mr->standardized_url = normalize_path(http_utils::standardize_url(t_url));
    mr->has_body = false;

    // log_access is now a response_sent alias (see webserver_aliases.cpp).
    resolve_method_callback(method, mr);

    return impl->requests_answer_first_step(connection, mr);
}

}  // namespace detail

}  // namespace httpserver
