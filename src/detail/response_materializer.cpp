/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// response_materializer behavior service (DR-014 §4.11). Logic moved
// verbatim out of the former detail/webserver_response_queue.cpp; that TU
// now holds only the thin webserver_impl::materialize_and_queue_response
// forwarder. Rewiring vs the original: the error paths call errors_
// (error_pages) instead of the in-class helpers; response_sent fires via
// hook_dispatch_; log_dispatch_error is the free function over config_.

#include "httpserver/detail/response_materializer.hpp"

#include <microhttpd.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "httpserver/create_webserver.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/response_body.hpp"
#include "httpserver/detail/dispatch_util.hpp"
#include "httpserver/detail/error_pages.hpp"
#include "httpserver/detail/hook_dispatcher.hpp"
#include "httpserver/detail/connection_context.hpp"

namespace httpserver {
namespace detail {

// materialize_response: ask the body to produce a fresh MHD_Response with
// no headers/footers/cookies attached. webserver_impl / response_materializer
// are friends of http_response so body_ is reachable directly.
MHD_Response* response_materializer::materialize_response(http_response* resp) {
    if (resp == nullptr || resp->body_ == nullptr) {
        return nullptr;
    }
    return resp->body_->materialize();
}

// decorate_mhd_response: walk the response's header/footer/cookie maps and
// attach each to the materialized MHD_Response.
void response_materializer::decorate_mhd_response(MHD_Response* response,
                                                  const http_response& resp) {
    for (const auto& [k, v] : resp.get_headers()) {
        MHD_add_response_header(response, k.c_str(), v.c_str());
    }
    for (const auto& [k, v] : resp.get_footers()) {
        MHD_add_response_footer(response, k.c_str(), v.c_str());
    }
    // Render from the structured cookie list (not the legacy cookies_ map)
    // via cookie::to_set_cookie_header() so attributes propagate to the wire
    // per RFC 6265 §4.1.
    for (const auto& c : resp.get_cookies_parsed()) {
        const std::string cookie_hdr = c.to_set_cookie_header();
        MHD_add_response_header(response, "Set-Cookie", cookie_hdr.c_str());
    }
}

struct MHD_Response* response_materializer::get_raw_response_with_fallback(
        detail::connection_context* conn) {
    // Every assignment into conn->response uses emplace(std::move(...)); the
    // optional owns the value and the deferred-body trampoline keeps a
    // pointer into it for the lifetime of the connection_context.
    auto try_materialize = [&]() -> struct MHD_Response* {
        return materialize_response(conn->response ? &*conn->response : nullptr);
    };
    auto emplace_and_materialize = [&](http_response r) -> struct MHD_Response* {
        conn->response.emplace(std::move(r));
        return try_materialize();
    };
    try {
        struct MHD_Response* raw = try_materialize();
        if (raw == nullptr) {
            // No exception, but the body materializer returned null. Route
            // through the safe internal-error path.
            return emplace_and_materialize(
                errors_.run_internal_error_handler_safely(
                    conn, "materialize_response returned null"));
        }
        return raw;
    } catch(const std::invalid_argument&) {
        try {
            return emplace_and_materialize(errors_.not_found_page(conn));
        } catch(...) {
            return nullptr;
        }
    } catch(const std::exception& e) {
        log_dispatch_error(config_, std::string("materialize threw: ") + e.what());
        try {
            return emplace_and_materialize(
                errors_.run_internal_error_handler_safely(conn, e.what()));
        } catch(...) {
            return nullptr;
        }
    } catch(...) {
        log_dispatch_error(config_, "materialize threw unknown exception");
        try {
            return emplace_and_materialize(
                errors_.run_internal_error_handler_safely(conn,
                    "unknown exception"));
        } catch(...) {
            return nullptr;
        }
    }
}

// Kind-dispatched queueing. For body_kind::digest_challenge, delegate to
// MHD_queue_auth_required_response3 so libmicrohttpd writes the RFC-7616
// WWW-Authenticate header with its HMAC-keyed nonce, our opaque, and the
// requested algorithm/qop/charset/userhash bits. Every other body kind goes
// through the standard MHD_queue_response path. The digest mapping is
// factored into the map_to_mhd_digest_args_ anonymous-namespace helper so
// the dispatcher stays under the CCN ceiling.
#ifdef HAVE_DAUTH
namespace {
struct mhd_digest_args {
    MHD_DigestAuthMultiAlgo3 algo;
    MHD_DigestAuthMultiQOP qop;
    const char* opaque_cstr;
    const char* domain_cstr;
};

mhd_digest_args map_to_mhd_digest_args_(
        const detail::digest_challenge_response_body::params& p,
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
    // qop="auth" is the only v2.0-supported variant; auth-int is parked.
    // qop_auth == false -> RFC-2069 no-qop.
    MHD_DigestAuthMultiQOP qop = p.qop_auth
        ? MHD_DIGEST_AUTH_MULT_QOP_AUTH
        : MHD_DIGEST_AUTH_MULT_QOP_NONE;
    // Empty user opaque -> substitute the per-webserver opaque.
    const char* opaque_cstr =
        p.opaque.empty() ? server_opaque.c_str() : p.opaque.c_str();
    const char* domain_cstr =
        p.domain.empty() ? nullptr : p.domain.c_str();
    return {algo, qop, opaque_cstr, domain_cstr};
}
}  // namespace
#endif  // HAVE_DAUTH

int response_materializer::queue_response_dispatching_kind(
        MHD_Connection* connection,
        detail::connection_context* conn,
        MHD_Response* raw_response) {
#ifdef HAVE_DAUTH
    if (conn->response->kind() == body_kind::digest_challenge) {
        auto* dch = static_cast<detail::digest_challenge_response_body*>(
            conn->response->body_);
        if (dch == nullptr) {
            // Defensive guard (CWE-476): kind() reported digest_challenge but
            // body_ is null. Fall back to the plain queue path.
            return static_cast<int>(MHD_queue_response(
                connection, conn->response->get_status(), raw_response));
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
        connection, conn->response->get_status(), raw_response));
}

MHD_Result response_materializer::materialize_and_queue_response(
        MHD_Connection* connection,
        detail::connection_context* conn,
        http_resource* resource) {
    struct MHD_Response* raw_response = get_raw_response_with_fallback(conn);
    if (raw_response == nullptr) {
        // Belt-and-suspenders: even get_raw_response_with_fallback's own
        // try/catch couldn't produce a response. Force the empty-body 500 so
        // MHD always has something to queue. Contract: log before the 500.
        log_dispatch_error(config_,
            "materialize_and_queue_response: "
            "get_raw_response_with_fallback returned null; "
            "forcing hardcoded empty-body 500");
        conn->response.emplace(
            errors_.internal_error_page(conn, "", /*force_our=*/true));
        raw_response = materialize_response(&*conn->response);
        if (raw_response == nullptr) {
            // Last-resort guard: internal_error_page's materialization also
            // returned null (e.g. under extreme memory pressure). Cannot call
            // decorate_mhd_response(nullptr, ...) -- that would pass a null
            // MHD_Response to MHD_add_response_header (CWE-476). Return MHD_NO
            // so the connection terminates gracefully rather than crashing.
            return (MHD_Result) MHD_NO;
        }
    }
    decorate_mhd_response(raw_response, *conn->response);
    int to_ret = queue_response_dispatching_kind(connection, conn, raw_response);
    // Fire response_sent AFTER MHD_queue_response (status/bytes reflect what
    // was queued) and BEFORE MHD_destroy_response (ctx.response backed by live
    // storage). MHD copies the response data during queue, so destroying the
    // MHD_Response below does not affect the queued bytes.
    hook_dispatch_.fire_response_sent_gated(conn, resource);
    // MHD reference-counting: for callback (deferred/streaming) responses MHD
    // increments its own refcount during queue, so this destroy only releases
    // the caller's reference. MHD keeps the streaming callback (and the cls
    // pointer into conn->response) alive until request_completed fires. The
    // connection_context (and conn->response) are destroyed only in the
    // request_completed callback, after MHD is done streaming.
    MHD_destroy_response(raw_response);
    return (MHD_Result) to_ret;
}

}  // namespace detail
}  // namespace httpserver
