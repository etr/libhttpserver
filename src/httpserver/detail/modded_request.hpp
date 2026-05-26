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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_MODDED_REQUEST_HPP_
#define SRC_HTTPSERVER_DETAIL_MODDED_REQUEST_HPP_

#include <microhttpd.h>  // TASK-020: MHD_destroy_post_processor (no longer reachable transitively via http_request.hpp -> http_utils.hpp)

#include <string>
#include <memory>
#include <optional>
#include <fstream>

#include "httpserver/http_method.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_response.hpp"

namespace httpserver {

namespace detail {

struct modded_request {
    struct MHD_PostProcessor *pp = nullptr;
    std::string complete_uri;
    std::string standardized_url;
    webserver* ws = nullptr;

    // TASK-036: pointer-to-member dispatch slot; render_* now return
    // http_response by value (PRD-RSP-REQ-007 / DR-004).
    http_response (httpserver::http_resource::*callback)(const httpserver::http_request&);

    // TASK-021: enum form of the wire method, decoded once at the
    // dispatch boundary in webserver_impl::answer_to_connection. Used
    // by finalize_answer to ask http_resource::is_allowed without a
    // per-request string compare. Defaults to count_ — a sentinel
    // outside the valid_method_mask, so is_allowed returns false for
    // unrecognized verbs (the 405 path).
    httpserver::http_method method_enum = httpserver::http_method::count_;

    std::unique_ptr<http_request> dhr = nullptr;
    // TASK-036 / DR-010 / §5.3: the response value lives here for the
    // full lifetime of the connection. The dispatch path moves the
    // handler's prvalue into this optional via emplace(); MHD's deferred
    // trampoline keeps a `cls` pointer into the deferred_body that lives
    // inside this http_response's SBO buffer. The optional is destroyed
    // by ~modded_request() in webserver_impl::request_completed, after
    // MHD has finished invoking the trampoline — so the captured
    // producer's state is released exactly once and exactly when DR-010
    // requires.
    std::optional<http_response> response_;
    bool has_body = false;
    // TASK-047: set by a pre-handler hook short-circuit (request_received
    // or body_chunk returning hook_action::respond_with(...)). When true,
    // finalize_answer skips resource resolution / auth / dispatch entirely
    // and goes straight to materialize_and_queue_response on the
    // pre-populated mr->response_. Defaults false; cleared only by the
    // modded_request destructor.
    bool skip_handler = false;

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
