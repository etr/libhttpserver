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

// Internal detail header: public headers only forward-declare
// detail::modded_request, so this file is gated to
// HTTPSERVER_COMPILATION-only, matching http_endpoint.hpp.
#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/modded_request.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_MODDED_REQUEST_HPP_
#define SRC_HTTPSERVER_DETAIL_MODDED_REQUEST_HPP_

#include <microhttpd.h>  // MHD_destroy_post_processor (not reachable transitively via http_request.hpp -> http_utils.hpp)

#include <chrono>
#include <string>
#include <memory>
#include <optional>
#include <fstream>

#include "httpserver/http_method.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_response.hpp"

namespace httpserver {

// modded_request forms a pointer-to-member on http_resource (the dispatch
// `callback` slot) and a std::weak_ptr<http_resource>; both need only a
// forward declaration. Declaring it here keeps this header self-contained
// so every dispatch-pipeline TU (DR-014) can include it directly without
// first pulling <httpserver/http_resource.hpp>.
class http_resource;

namespace detail {

// modded_request adapts the raw MHD connection data into the
// libhttpserver request model, accumulating per-connection state
// (parsed headers, upload stream, method callback, staged response)
// across MHD's repeated answer_to_connection invocations for one
// HTTP request until finalize_answer queues the response.
//
// Request lifecycle (MHD callback sequence):
//   1. uri_log (webserver_callbacks.cpp) allocates this struct; MHD
//      stores the returned pointer in *con_cls and hands it back to
//      every later callback for the same request.
//   2. answer_to_connection (webserver_request.cpp) fires one or more
//      times; `request == nullptr` marks the FIRST invocation, which stamps
//      start_time, ws, standardized_url, method_enum and builds request.
//   3. Body chunks arrive via requests_answer_second_step
//      (webserver_body_pipeline.cpp); a hook short-circuit there sets
//      skip_handler, which drains remaining chunks and later makes
//      finalize_answer bypass routing/auth/dispatch.
//   4. The zero-size upload callback signals end-of-body and routes to
//      complete_request -> finalize_answer, which stages `response`.
//   5. MHD's completion callback (request_completed) fires the
//      request_completed hook, then deletes this object.
struct modded_request {
    struct MHD_PostProcessor *pp = nullptr;
    std::string complete_uri;
    std::string standardized_url;
    webserver* ws = nullptr;

    // Pointer-to-member dispatch slot; render_* return http_response
    // by value. Initialized to
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

    // The http_request object for this connection, accessed throughout
    // dispatch as mr->request. Null until the first answer_to_connection
    // invocation constructs it.
    std::unique_ptr<http_request> request = nullptr;
    // Anchor kept alive until request_completed. See webserver_impl.hpp for the full contract.
    std::optional<http_response> response;
    bool has_body = false;
    // Set by a pre-handler hook short-circuit (request_received
    // or body_chunk returning hook_action::respond_with(...)). When true,
    // finalize_answer skips resource resolution / auth / dispatch entirely
    // and goes straight to materialize_and_queue_response on the
    // pre-populated mr->response. Defaults false; cleared only by the
    // modded_request destructor.
    bool skip_handler = false;

    // Monotone counter of body bytes delivered to this
    // request's body_chunk hook. Incremented on every chunk regardless of
    // whether grow_content() is called (which is gated on pp == nullptr ||
    // put_processed_data_to_content). Using get_content().size() for the
    // hook's ctx.offset was wrong when put_processed_data_to_content is
    // false and a post-processor is active -- grow_content is skipped so
    // the content buffer stays empty while bytes DO arrive. This field
    // accumulates independently and is the source of truth for ctx.offset.
    std::uint64_t body_bytes_seen = 0;

    // Captured by resolve_resource_for_request when a route matched.
    // The pair populates route_descriptor for both the route_resolved
    // and before_handler firing sites. Lifetime is the request (i.e.,
    // the modded_request itself), so the string_view in the descriptor
    // stays valid across hook calls even if a concurrent
    // unregister_path erases the underlying registration.
    //   - matched_path_template: CAUTION -- despite the name, this
    //     currently holds the canonicalized REQUEST URL
    //     (standardized_url), not the registered route pattern: the v2
    //     lookup result does not retain the matched pattern text, so
    //     resolve_resource_for_request falls back to the concrete URL.
    //     Hook consumers reading route_descriptor::path_template see
    //     the request URL. Empty when no match (404 path).
    //   - matched_is_prefix: true for register_prefix / family-url
    //     registrations; false for exact-path registrations.
    std::string matched_path_template;
    bool matched_is_prefix = false;

    std::string upload_key;
    std::string upload_filename;
    std::unique_ptr<std::ofstream> upload_ostrm;

    // Captured once on the first invocation of
    // webserver_impl::answer_to_connection for this request (i.e., when
    // mr->request is still null -- the "fresh request" branch). The
    // response_sent and request_completed firing sites measure elapsed
    // wall time from this anchor. Default-constructed value (epoch) is
    // never read because both fire sites are unreachable until
    // answer_to_connection has set this field.
    std::chrono::steady_clock::time_point start_time{};

    // weak_ptr to the resource that handled this request.
    // Populated in finalize_answer when resolve_resource_for_request
    // returns a non-null hrm. Used by fire_request_completed_gated (which
    // fires from the MHD completion callback, after finalize_answer's
    // owning shared_ptr is gone) and the handler_exception path. If the
    // resource was unregistered between dispatch and completion, lock()
    // returns null and the per-route chain is skipped.
    //
    // The two in-scope firing helpers (fire_after_handler_gated /
    // fire_response_sent_gated) do NOT lock() this weak_ptr: they run
    // while finalize_answer's owning shared_ptr is still alive, so the
    // resolved resource is passed to them directly. Locking there cost a
    // control-block CAS per matched request even with zero hooks -- the
    // gate is checked first now instead.
    std::weak_ptr<http_resource> resource_weak_{};

    // Snapshot, taken in finalize_answer, of whether the resolved
    // resource carried a per-route hook table at dispatch time. Lets
    // fire_request_completed_gated skip the weak_ptr lock() (and its
    // control-block atomics) on the overwhelmingly common zero-per-route-
    // hook path: when this is false and no server-wide request_completed
    // hook is registered, no lock is taken at all. The precise
    // any_hooks(request_completed) check still happens after locking when
    // this is true, preserving the unregistration-skip contract.
    bool route_has_hook_table_ = false;

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
