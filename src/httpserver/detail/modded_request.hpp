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

// ADR-002 / TASK-014: PIMPL split removed the transitive include of
// modded_request.hpp from public headers (webserver.hpp now only forward-
// declares detail::modded_request). The gate is tightened to
// HTTPSERVER_COMPILATION-only, matching http_endpoint.hpp.
#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/modded_request.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_MODDED_REQUEST_HPP_
#define SRC_HTTPSERVER_DETAIL_MODDED_REQUEST_HPP_

#include <microhttpd.h>  // TASK-020: MHD_destroy_post_processor (no longer reachable transitively via http_request.hpp -> http_utils.hpp)

#include <chrono>
#include <string>
#include <memory>
#include <optional>
#include <fstream>

#include "httpserver/http_method.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_response.hpp"

namespace httpserver {

namespace detail {

// modded_request adapts the raw MHD connection data into the
// libhttpserver request model, accumulating per-connection state
// (parsed headers, upload stream, method callback, staged response)
// across MHD's repeated answer_to_connection invocations for one
// HTTP request until finalize_answer queues the response.
struct modded_request {
    struct MHD_PostProcessor *pp = nullptr;
    std::string complete_uri;
    std::string standardized_url;
    webserver* ws = nullptr;

    // TASK-036: pointer-to-member dispatch slot; render_* now return
    // http_response by value (PRD-RSP-REQ-007 / DR-004). Initialized to
    // nullptr; set by resolve_method_callback for recognized HTTP methods.
    // For unrecognized methods mr->method_enum is left at count_ and
    // finalize_answer takes the 405 path before invoking this pointer.
    http_response (http_resource::*callback)(const http_request&) = nullptr;

    // Enum form of the wire method, decoded once at the dispatch boundary
    // in webserver_impl::answer_to_connection. Used by finalize_answer to
    // ask http_resource::is_allowed without a per-request string compare.
    // Defaults to count_ — a sentinel outside the valid_method_mask, so
    // is_allowed returns false for unrecognized verbs (the 405 path).
    http_method method_enum = http_method::count_;

    std::unique_ptr<http_request> dhr = nullptr;
    // DR-010 / §5.3: anchor kept alive until request_completed. See webserver_impl.hpp for full contract.
    std::optional<http_response> response;
    bool has_body = false;
    // TASK-047: set by a pre-handler hook short-circuit (request_received
    // or body_chunk returning hook_action::respond_with(...)). When true,
    // finalize_answer skips resource resolution / auth / dispatch entirely
    // and goes straight to materialize_and_queue_response on the
    // pre-populated mr->response. Defaults false; cleared only by the
    // modded_request destructor.
    bool skip_handler = false;

    // TASK-047 review: monotone counter of body bytes delivered to this
    // request's body_chunk hook. Incremented on every chunk regardless of
    // whether grow_content() is called (which is gated on pp == nullptr ||
    // put_processed_data_to_content). Using get_content().size() for the
    // hook's ctx.offset was wrong when put_processed_data_to_content is
    // false and a post-processor is active -- grow_content is skipped so
    // the content buffer stays empty while bytes DO arrive. This field
    // accumulates independently and is the source of truth for ctx.offset.
    std::uint64_t body_bytes_seen = 0;

    // TASK-048: captured by resolve_resource_for_request when a route
    // matched. The pair populates route_descriptor for both the
    // route_resolved and before_handler firing sites. Lifetime is the
    // request (i.e., the modded_request itself), so the string_view in
    // the descriptor stays valid across hook calls even if a concurrent
    // unregister_path erases the underlying registration.
    //   - matched_path_template: owning copy of the matched endpoint's
    //     url_complete. Empty when no match (404 path).
    //   - matched_is_prefix: true for register_prefix / family-url
    //     registrations; false for exact-path registrations.
    std::string matched_path_template;
    bool matched_is_prefix = false;

    std::string upload_key;
    std::string upload_filename;
    std::unique_ptr<std::ofstream> upload_ostrm;

    // TASK-050: captured once on the first invocation of
    // webserver_impl::answer_to_connection for this request (i.e., when
    // mr->dhr is still null -- the "fresh request" branch). The
    // response_sent and request_completed firing sites measure elapsed
    // wall time from this anchor. Default-constructed value (epoch) is
    // never read because both fire sites are unreachable until
    // answer_to_connection has set this field.
    std::chrono::steady_clock::time_point start_time{};

    // TASK-051: weak_ptr to the resource that handled this request.
    // Populated in finalize_answer when resolve_resource_for_request
    // returns a non-null hrm. Used by fire_response_sent_gated and
    // fire_request_completed_gated to fire the per-route phase chain
    // after the server-wide one. If the resource was unregistered
    // between dispatch and completion, lock() returns null and the
    // per-route chain is skipped (the action-item contract). The
    // weak_ptr also keeps a control-block reference into the resource
    // alive until ~modded_request, so the resource cannot be destroyed
    // mid-firing -- the hot-path firing helpers lock() into a local
    // shared_ptr before iterating.
    std::weak_ptr<http_resource> resource_weak_{};

    modded_request() = default;

    modded_request(const modded_request& b) = delete;
    modded_request(modded_request&& b) = default;

    modded_request& operator=(const modded_request& b) = delete;
    modded_request& operator=(modded_request&& b) = default;

    ~modded_request() {
        if (nullptr != pp) {
            MHD_destroy_post_processor(pp);
        }
    }
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_MODDED_REQUEST_HPP_
