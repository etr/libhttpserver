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

#include <microhttpd.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "httpserver/detail/body.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

namespace detail {

// webserver_response_queue.cpp -- response materialization and queueing.
// Carved out of src/detail/webserver_request.cpp in TASK-086 to keep both
// translation units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). Holds the materialize_response /
// decorate_mhd_response / get_raw_response_with_fallback /
// queue_response_dispatching_kind / materialize_and_queue_response members
// (and their anonymous-namespace mhd_digest_args helper). No behaviour
// change: the bodies are moved verbatim.
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
    // TASK-064: render from the structured cookie list, not from the
    // legacy `cookies_` map. Each entry is rendered via
    // cookie::to_set_cookie_header() so attributes (Domain, Path,
    // Expires, Max-Age, Secure, HttpOnly, SameSite) propagate to the
    // wire per RFC 6265 §4.1. The legacy `cookies_` mirror is no
    // longer a render source -- it survives only to back the
    // deprecated `get_cookie`/`get_cookies` accessors.
    for (const auto& c : resp.get_cookies_parsed()) {
        const std::string cookie_hdr = c.to_set_cookie_header();
        MHD_add_response_header(response, "Set-Cookie", cookie_hdr.c_str());
    }
}

struct MHD_Response* webserver_impl::get_raw_response_with_fallback(detail::modded_request* mr) {
    // TASK-036 / DR-010: every assignment into mr->response uses
    // emplace(std::move(...)); the optional owns the value and the
    // deferred-body trampoline keeps a pointer into it for the lifetime
    // of the modded_request.
    auto try_materialize = [&]() -> struct MHD_Response* {
        return materialize_response(mr->response ? &*mr->response : nullptr);
    };
    // Helper: emplace a new response and immediately materialize it.
    // Each catch arm sets mr->response then returns the raw MHD handle.
    auto emplace_and_materialize = [&](http_response r) -> struct MHD_Response* {
        mr->response.emplace(std::move(r));
        return try_materialize();
    };
    try {
        struct MHD_Response* raw = try_materialize();
        if (raw == nullptr) {
            // TASK-031: no exception was thrown, but the body materializer
            // returned null. Route through the safe internal-error path so
            // a misbehaving user handler can't escape.
            return emplace_and_materialize(run_internal_error_handler_safely(
                mr, "materialize_response returned null"));
        }
        return raw;
    } catch(const std::invalid_argument&) {
        try {
            return emplace_and_materialize(not_found_page(mr));
        } catch(...) {
            return nullptr;
        }
    } catch(const std::exception& e) {
        log_dispatch_error(std::string("materialize threw: ") + e.what());
        try {
            return emplace_and_materialize(
                run_internal_error_handler_safely(mr, e.what()));
        } catch(...) {
            return nullptr;
        }
    } catch(...) {
        log_dispatch_error("materialize threw unknown exception");
        try {
            return emplace_and_materialize(run_internal_error_handler_safely(
                mr, "unknown exception"));
        } catch(...) {
            return nullptr;
        }
    }
}

// TASK-050: validate_websocket_handshake / complete_websocket_upgrade /
// try_handle_websocket_upgrade moved to src/detail/webserver_websocket.cpp
// to keep this TU under the FILE_LOC_MAX gate after adding the
// after_handler / response_sent firing sites.

// TASK-062: kind-dispatched queueing.
//
// For `body_kind::digest_challenge`, delegate to
// MHD_queue_auth_required_response3 so libmicrohttpd writes the
// authoritative RFC-7616 WWW-Authenticate header with its
// HMAC-keyed nonce, our opaque, and the requested
// algorithm/qop/charset/userhash bits. For every other body kind,
// queue the response through the standard MHD_queue_response path.
// Returning `int` matches the legacy MHD_queue_response signature on
// older MHD versions (the MHD_Result alias upcast happens at the
// call site).
//
// The digest mapping (algorithm enum -> MHD bitfield, opaque
// substitution, domain optional-NULL handling) is factored into the
// `map_to_mhd_digest_args_` anonymous-namespace helper so the
// member-function dispatcher stays under the CCN ceiling. That helper
// only handles fields visible through `const params&`, which is safe
// to expose because get_params() is public.
#ifdef HAVE_DAUTH
namespace {
struct mhd_digest_args {
    MHD_DigestAuthMultiAlgo3 algo;
    MHD_DigestAuthMultiQOP qop;
    const char* opaque_cstr;
    const char* domain_cstr;
};

mhd_digest_args map_to_mhd_digest_args_(
        const detail::digest_challenge_body::params& p,
        const std::string& server_opaque) {
    MHD_DigestAuthMultiAlgo3 algo;
    switch (p.algorithm) {
        case http::http_utils::digest_algorithm::SHA256:
            algo = MHD_DIGEST_AUTH_MULT_ALGO3_SHA256;
            break;
        case http::http_utils::digest_algorithm::SHA512_256:
            algo = MHD_DIGEST_AUTH_MULT_ALGO3_SHA512_256;
            break;
        case http::http_utils::digest_algorithm::MD5:
        default:
            algo = MHD_DIGEST_AUTH_MULT_ALGO3_MD5;
            break;
    }
    // qop="auth" is the only v2.0-supported variant; auth-int is parked
    // (TASK-062 plan §7). qop_auth == false -> RFC-2069 no-qop.
    MHD_DigestAuthMultiQOP qop = p.qop_auth
        ? MHD_DIGEST_AUTH_MULT_QOP_AUTH
        : MHD_DIGEST_AUTH_MULT_QOP_NONE;
    // Empty user opaque -> substitute per-webserver opaque (plan §2.4).
    const char* opaque_cstr =
        p.opaque.empty() ? server_opaque.c_str() : p.opaque.c_str();
    const char* domain_cstr =
        p.domain.empty() ? nullptr : p.domain.c_str();
    return {algo, qop, opaque_cstr, domain_cstr};
}
}  // namespace
#endif  // HAVE_DAUTH

int webserver_impl::queue_response_dispatching_kind(
        MHD_Connection* connection,
        detail::modded_request* mr,
        MHD_Response* raw_response) {
#ifdef HAVE_DAUTH
    if (mr->response->kind() == body_kind::digest_challenge) {
        auto* dch = static_cast<detail::digest_challenge_body*>(
            mr->response->body_);
        if (dch == nullptr) {
            // Defensive guard (CWE-476): kind() reported digest_challenge
            // but body_ is null. Fall back to the plain queue path instead
            // of dereferencing a null pointer in get_params() below.
            return static_cast<int>(MHD_queue_response(
                connection, mr->response->get_status(), raw_response));
        }
        const auto& p = dch->get_params();
        auto args = map_to_mhd_digest_args_(p, digest_opaque_);
        return static_cast<int>(MHD_queue_auth_required_response3(
            connection,
            p.realm.c_str(),
            args.opaque_cstr,
            args.domain_cstr,
            raw_response,
            p.signal_stale ? MHD_YES : MHD_NO,
            args.qop,
            args.algo,
            p.userhash_support ? MHD_YES : MHD_NO,
            p.prefer_utf8 ? MHD_YES : MHD_NO));
    }
#endif  // HAVE_DAUTH
    return static_cast<int>(MHD_queue_response(
        connection, mr->response->get_status(), raw_response));
}

MHD_Result webserver_impl::materialize_and_queue_response(MHD_Connection* connection,
                                                          detail::modded_request* mr) {
    struct MHD_Response* raw_response = get_raw_response_with_fallback(mr);
    if (raw_response == nullptr) {
        // Belt-and-suspenders: even get_raw_response_with_fallback's
        // own try/catch couldn't produce a response. Force the
        // empty-body 500 fallback so MHD always has something to queue.
        // DR-009 §5.2 point 4: library logs before sending hardcoded 500.
        log_dispatch_error(
            "materialize_and_queue_response: "
            "get_raw_response_with_fallback returned null; "
            "forcing hardcoded empty-body 500");
        mr->response.emplace(internal_error_page(mr, "", /*force_our=*/true));
        raw_response = materialize_response(&*mr->response);
        if (raw_response == nullptr) {
            // Last-resort guard: internal_error_page's materialization
            // also returned null (e.g. under extreme memory pressure).
            // Cannot call decorate_mhd_response(nullptr, ...) — that would
            // pass a null MHD_Response to MHD_add_response_header, causing
            // a null-dereference (CWE-476). Return MHD_NO so the connection
            // is terminated gracefully rather than crashing the server.
            return (MHD_Result) MHD_NO;
        }
    }
    decorate_mhd_response(raw_response, *mr->response);
    int to_ret = queue_response_dispatching_kind(connection, mr, raw_response);
    // TASK-050: fire response_sent AFTER MHD_queue_response (so the
    // status/bytes ctx fields reflect what was actually queued) and
    // BEFORE MHD_destroy_response (so ctx.response is backed by live
    // storage). MHD copies the response data from raw_response during
    // queue, so destroying the MHD_Response below does not affect the
    // queued bytes -- only the http_response in mr->response matters
    // for the hook ctx pointer.
    fire_response_sent_gated(mr);
    // MHD reference-counting note: for MHD_create_response_from_callback
    // responses (deferred/streaming), MHD increments its own internal
    // refcount during MHD_queue_response, so this MHD_destroy_response only
    // releases the *caller's* reference. MHD keeps the streaming callback
    // (deferred_body::trampoline) alive — and therefore the cls pointer into
    // mr->response — until request_completed fires and MHD releases its own
    // reference. The modded_request (and mr->response) are destroyed only in
    // the request_completed callback, which is after MHD is done streaming.
    MHD_destroy_response(raw_response);
    return (MHD_Result) to_ret;
}

}  // namespace detail

}  // namespace httpserver
