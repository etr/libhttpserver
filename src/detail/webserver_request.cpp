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


namespace {

// Apply one path segment to the running stack: "" / "." are skipped,
// ".." pops, anything else pushes. Pulled out of normalize_path so the
// caller stays a flat tokenize-and-rebuild loop.
void apply_normalized_segment(std::vector<std::string>& segments,
                              const std::string& seg) {
    if (seg == "..") {
        if (!segments.empty()) segments.pop_back();
        return;
    }
    if (seg.empty() || seg == ".") return;
    segments.push_back(seg);
}

}  // namespace

static std::string normalize_path(const std::string& path) {
    std::vector<std::string> segments;
    std::string::size_type start = 0;
    if (!path.empty() && path[0] == '/') start = 1;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        apply_normalized_segment(segments, path.substr(start, end - start));
        start = end + 1;
    }
    std::string normalized = "/";
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) normalized += "/";
        normalized += segments[i];
    }
    return normalized;
}

namespace detail {

bool webserver_impl::should_skip_auth(const std::string& path) const {
    std::string normalized = normalize_path(path);

    for (const auto& skip_path : parent->auth_skip_paths) {
        if (skip_path == normalized) return true;
        // Support wildcard suffix (e.g., "/public/*")
        if (skip_path.size() > 2 && skip_path.back() == '*' &&
            skip_path[skip_path.size() - 2] == '/') {
            std::string prefix = skip_path.substr(0, skip_path.size() - 1);
            if (normalized.compare(0, prefix.size(), prefix) == 0) return true;
        }
    }
    return false;
}

// TASK-047: requests_answer_first_step and requests_answer_second_step
// moved to detail/webserver_body_pipeline.cpp to keep this TU under the
// 500-LOC ceiling (FILE_LOC_MAX in scripts/check-file-size.sh) once the
// request_received and body_chunk hook firing sites landed.

// TASK-013: dispatch helpers replacing the v1 `get_raw_response`,
// `decorate_response`, and `enqueue_response` virtuals on http_response.
// Now that http_response is a final value type and the v1 polymorphic
// subclass hierarchy is gone, the wire-construction logic lives here in
// the dispatch path. webserver_impl is a friend of http_response (declared
// in http_response.hpp) so it can reach body_ directly.
//
// materialize_response: ask the body to produce a fresh MHD_Response
// with no headers/footers/cookies attached.
//
// decorate_mhd_response: walk the response's header/footer/cookie maps
// and attach each to the materialized MHD_Response.
MHD_Response* webserver_impl::materialize_response(http_response* resp) {
    if (resp == nullptr || resp->body_ == nullptr) {
        return nullptr;
    }
    return resp->body_->materialize();
}

void webserver_impl::decorate_mhd_response(MHD_Response* response,
                                      const http_response& resp) {
    for (const auto& [k, v] : resp.get_headers()) {
        MHD_add_response_header(response, k.c_str(), v.c_str());
    }
    for (const auto& [k, v] : resp.get_footers()) {
        MHD_add_response_footer(response, k.c_str(), v.c_str());
    }
    for (const auto& [k, v] : resp.get_cookies()) {
        MHD_add_response_header(response, "Set-Cookie",
                                (k + "=" + v).c_str());
    }
}

struct MHD_Response* webserver_impl::get_raw_response_with_fallback(detail::modded_request* mr) {
    // TASK-036 / DR-010: every assignment into mr->response_ uses
    // emplace(std::move(...)); the optional owns the value and the
    // deferred-body trampoline keeps a pointer into it for the lifetime
    // of the modded_request.
    auto materialize_current = [&]() -> struct MHD_Response* {
        return materialize_response(mr->response_ ? &*mr->response_ : nullptr);
    };
    try {
        struct MHD_Response* raw = materialize_current();
        if (raw == nullptr) {
            // TASK-031: no exception was thrown, but the body materializer
            // returned null. Route through the safe internal-error path so
            // a misbehaving user handler can't escape.
            mr->response_.emplace(run_internal_error_handler_safely(
                mr, "materialize_response returned null"));
            raw = materialize_current();
        }
        return raw;
    } catch(const std::invalid_argument&) {
        try {
            mr->response_.emplace(not_found_page(mr));
            return materialize_current();
        } catch(...) {
            return nullptr;
        }
    } catch(const std::exception& e) {
        log_dispatch_error(std::string("materialize threw: ") + e.what());
        try {
            mr->response_.emplace(run_internal_error_handler_safely(mr, e.what()));
            return materialize_current();
        } catch(...) {
            return nullptr;
        }
    } catch(...) {
        log_dispatch_error("materialize threw unknown exception");
        try {
            mr->response_.emplace(run_internal_error_handler_safely(mr,
                                                         "unknown exception"));
            return materialize_current();
        } catch(...) {
            return nullptr;
        }
    }
}

#ifdef HAVE_WEBSOCKET
std::optional<const char*>
webserver_impl::validate_websocket_handshake(MHD_Connection* connection) {
    const char* connection_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                                MHD_HTTP_HEADER_CONNECTION);
    const char* ws_version = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                         "Sec-WebSocket-Version");
    const char* ws_key = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                     "Sec-WebSocket-Key");
    if (connection_header == nullptr || strcasestr(connection_header, "Upgrade") == nullptr) {
        return std::nullopt;
    }
    if (ws_version == nullptr || strcmp(ws_version, "13") != 0) {
        return std::nullopt;
    }
    if (ws_key == nullptr || ws_key[0] == '\0') {
        return std::nullopt;
    }
    return ws_key;
}

std::optional<MHD_Result>
webserver_impl::complete_websocket_upgrade(MHD_Connection* connection,
                                           detail::modded_request* mr,
                                           const char* ws_key) {
    std::shared_lock lock(registered_resources_mutex);
    auto ws_it = registered_ws_handlers.find(mr->standardized_url);
    if (ws_it == registered_ws_handlers.end()) {
        return std::nullopt;
    }
    // TASK-035: take a shared_ptr copy under the shared lock so the
    // handler is kept alive across the MHD upgrade callback even if
    // unregister_ws_resource erases the slot mid-upgrade.
    std::shared_ptr<websocket_handler> handler_sp = ws_it->second;
    lock.unlock();

    auto* data = new ws_upgrade_data{this, std::move(handler_sp)};
    struct MHD_Response* response = MHD_create_response_for_upgrade(
        &webserver_impl::upgrade_handler, data);
    if (response == nullptr) {
        delete data;
        return std::nullopt;
    }
    MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, "websocket");

    // Compute Sec-WebSocket-Accept from client's key (RFC 6455 §4.2.2).
    // Base64 of SHA-1 = 28 chars + null.
    char accept_header[29];
    if (MHD_websocket_create_accept_header(ws_key, accept_header) == MHD_WEBSOCKET_STATUS_OK) {
        MHD_add_response_header(response, "Sec-WebSocket-Accept", accept_header);
    }
    MHD_Result to_ret = (MHD_Result) MHD_queue_response(connection,
                                                       MHD_HTTP_SWITCHING_PROTOCOLS,
                                                       response);
    MHD_destroy_response(response);
    return to_ret;
}
#endif  // HAVE_WEBSOCKET

std::optional<MHD_Result>
webserver_impl::try_handle_websocket_upgrade(MHD_Connection* connection,
                                             detail::modded_request* mr) {
#ifdef HAVE_WEBSOCKET
    const char* upgrade_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                             MHD_HTTP_HEADER_UPGRADE);
    if (upgrade_header == nullptr || strcasecmp(upgrade_header, "websocket") != 0) {
        return std::nullopt;
    }
    auto ws_key = validate_websocket_handshake(connection);
    if (!ws_key) {
        // RFC 6455 §4.2.1: required handshake header missing or malformed.
        struct MHD_Response* bad_response = MHD_create_response_from_buffer(0, nullptr,
                                                                            MHD_RESPMEM_PERSISTENT);
        MHD_Result ret = (MHD_Result) MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST,
                                                         bad_response);
        MHD_destroy_response(bad_response);
        return ret;
    }
    return complete_websocket_upgrade(connection, mr, *ws_key);
#else
    (void)connection;
    (void)mr;
    return std::nullopt;
#endif  // HAVE_WEBSOCKET
}
MHD_Result webserver_impl::materialize_and_queue_response(MHD_Connection* connection,
                                                          detail::modded_request* mr) {
    struct MHD_Response* raw_response = get_raw_response_with_fallback(mr);
    if (raw_response == nullptr) {
        // Belt-and-suspenders: even get_raw_response_with_fallback's
        // own try/catch couldn't produce a response. Force the
        // empty-body 500 fallback so MHD always has something to queue.
        mr->response_.emplace(internal_error_page(mr, "", /*force_our=*/true));
        raw_response = materialize_response(&*mr->response_);
    }
    decorate_mhd_response(raw_response, *mr->response_);
    int to_ret = MHD_queue_response(connection, mr->response_->get_status(), raw_response);
    MHD_destroy_response(raw_response);
    return (MHD_Result) to_ret;
}

// TASK-048: gated fire of the route_resolved phase. Built as a small
// helper so finalize_answer stays under the per-function CCN ceiling
// (CCN_MAX in scripts/check-complexity.sh) after the addition of the
// hook firing site. Observation-only per DR-012 §4.10.
static void fire_route_resolved_gated(webserver_impl* impl,
                                      detail::modded_request* mr,
                                      bool found,
                                      const std::shared_ptr<http_resource>& hrm) {
    if (!impl->any_hooks_[static_cast<std::size_t>(hook_phase::route_resolved)]
            .load(std::memory_order_relaxed)) {
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
        /*request=*/mr->dhr.get(),
        /*matched=*/std::move(desc),
        /*resource=*/hrm ? hrm.get() : nullptr};
    impl->fire_route_resolved(ctx);
}

MHD_Result webserver_impl::finalize_answer(MHD_Connection* connection,
                                           struct detail::modded_request* mr) {
    if (auto ws_result = try_handle_websocket_upgrade(connection, mr)) {
        return *ws_result;
    }

    // TASK-047: a pre-handler short-circuit hook (request_received or
    // body_chunk) already populated mr->response_. Skip route lookup,
    // auth, and handler dispatch -- go straight to the response queue.
    if (mr->skip_handler) {
        return materialize_and_queue_response(connection, mr);
    }

    // TASK-023: hold a shared_ptr copy across dispatch. If a concurrent
    // unregister_resource erases the route mid-call, the resource stays
    // alive until our local shared_ptr drops at the end of
    // finalize_answer.
    std::shared_ptr<http_resource> hrm;
    bool found = resolve_resource_for_request(mr, hrm);

    fire_route_resolved_gated(this, mr, found, hrm);

    // TASK-048: fire before_handler from finalize_answer (not from inside
    // dispatch_resource_handler). This ensures auth and method-not-allowed
    // alias hooks run as part of the unified before_handler chain, with
    // the auth alias as the first hook (registered in
    // install_default_alias_hooks_). The gate preserves zero-cost-when-unused.
    if (found &&
        any_hooks_[static_cast<std::size_t>(hook_phase::before_handler)]
            .load(std::memory_order_relaxed)) {
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
        if (auto sc = fire_before_handler(ctx)) {
            mr->response_.emplace(std::move(*sc));
            return materialize_and_queue_response(connection, mr);
        }
    }

    if (found) {
        dispatch_resource_handler(mr, hrm);
    } else if (!mr->response_) {
        mr->response_.emplace(not_found_page(mr));
    }

    return materialize_and_queue_response(connection, mr);
}

MHD_Result webserver_impl::complete_request(MHD_Connection* connection, struct detail::modded_request* mr, const char* version, const char* method) {
    mr->ws = parent;

    mr->dhr->set_path(mr->standardized_url);
    mr->dhr->set_method(method);
    mr->dhr->set_version(version);

    return finalize_answer(connection, mr);
}

void webserver_impl::resolve_method_callback(const char* method,
                                              detail::modded_request* mr) {
    // Case-sensitive per RFC 7230 §3.1.1: HTTP method is case-sensitive.
    // TASK-021: also record the enum form once so finalize_answer can
    // call hrm->is_allowed without re-scanning the wire string.
    // Unrecognised methods leave mr->method_enum at the default
    // (count_), so is_allowed(count_) returns false and the request
    // takes the 405 path. Pre-existing latent bug: mr->callback may
    // also be left un-set here; see TASK-027 for the dispatch redesign.
    if (0 == strcmp(method, http_utils::http_method_get)) {
        mr->callback = &http_resource::render_get;
        mr->method_enum = http_method::get;
    } else if (0 == strcmp(method, http_utils::http_method_post)) {
        mr->callback = &http_resource::render_post;
        mr->method_enum = http_method::post;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_put)) {
        mr->callback = &http_resource::render_put;
        mr->method_enum = http_method::put;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_delete)) {
        mr->callback = &http_resource::render_delete;
        mr->method_enum = http_method::del;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_patch)) {
        mr->callback = &http_resource::render_patch;
        mr->method_enum = http_method::patch;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_head)) {
        mr->callback = &http_resource::render_head;
        mr->method_enum = http_method::head;
    } else if (0 == strcmp(method, http_utils::http_method_connect)) {
        mr->callback = &http_resource::render_connect;
        mr->method_enum = http_method::connect;
    } else if (0 == strcmp(method, http_utils::http_method_trace)) {
        mr->callback = &http_resource::render_trace;
        mr->method_enum = http_method::trace;
    } else if (0 == strcmp(method, http_utils::http_method_options)) {
        mr->callback = &http_resource::render_options;
        mr->method_enum = http_method::options;
    }
}

MHD_Result webserver_impl::answer_to_connection(void* cls, MHD_Connection* connection, const char* url, const char* method,
        const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls) {
    auto* mr = static_cast<detail::modded_request*>(*con_cls);
    auto* impl = static_cast<webserver_impl*>(cls);

    if (mr->dhr) {
        return impl->requests_answer_second_step(connection, method, version,
                                                  upload_data, upload_data_size, mr);
    }

    const MHD_ConnectionInfo* conninfo =
        MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);
    if (conninfo != nullptr && impl->parent->tcp_nodelay) {
        int yes = 1;
        setsockopt(conninfo->connect_fd, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<char*>(&yes), sizeof(int));
    }

    std::string t_url = url;
    base_unescaper(&t_url, impl->parent->unescaper);
    mr->standardized_url = http_utils::standardize_url(t_url);
    mr->has_body = false;

    webserver_impl::access_log(impl->parent, mr->complete_uri + " METHOD: " + method);
    resolve_method_callback(method, mr);

    return impl->requests_answer_first_step(connection, mr);
}

}  // namespace detail

}  // namespace httpserver
