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

// webserver_impl.cpp -- the composition-root translation unit for
// detail::webserver_impl.
//
// Holds the impl's construction/destruction (the DR-014 collaborator +
// service wiring in the member-initialiser list, and the GnuTLS SNI-cache
// teardown) plus the small non-trampoline member glue that has no better
// home:
//   * serialize_allow_methods -- a thin test seam over
//     detail::format_allow_header (production dispatch calls
//     hrm->get_allow_header() directly);
//   * prepare_or_create_lambda_shim / commit_handlers_to_shim -- the
//     lambda_resource shim lifecycle for on_*/route registration (the
//     table probe + mutation live in the route_table collaborator; these
//     run inside the caller's routes_.lock_for_write() window).
//
// The MHD C-ABI trampolines live in the adapter TUs (webserver_request.cpp /
// webserver_callbacks.cpp); the public webserver:: surface lives in
// webserver.cpp and the webserver_*.cpp API-area files. Consolidated here
// (from the former webserver.cpp ctor/dtor block, webserver_dispatch.cpp, and
// webserver_routes_upsert.cpp) so that webserver_impl maps to a small set of
// single-class TUs rather than a scatter of one-function files.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <microhttpd.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

#include "httpserver/create_webserver.hpp"
#include "httpserver/http_method.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/method_utils.hpp"
#include "httpserver/detail/route_entry.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif  // HAVE_GNUTLS

namespace httpserver {

namespace {

// Iterate enum-declaration order (get, head, post, ...) over the bits
// set in @p methods, invoking @p fn for each. Used by the on_methods_
// pre-check loop and the commit loop; pulling the scaffolding into a
// single helper dedupes the iteration boilerplate. The order matches
// http_method enum-declaration order, which is also the serialization
// order for the `Allow:` header.
template <typename Fn>
void for_each_requested_method(method_set methods, Fn&& fn) {
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (methods.contains(m)) fn(m);
    }
}

}  // namespace

namespace detail {

// ----- webserver_impl construction / destruction -------------------------

// Seed `digest_opaque_` with 16 random bytes from
// std::random_device, hex-encoded -> 32-char string. RFC 7616 §5.10:
// opaque is an identifier, not a secret -- std::random_device gives the
// "unpredictable enough that clients cannot guess server state"
// property the RFC requires; the strong nonce HMAC keying is owned by
// libmicrohttpd (MHD_OPTION_DIGEST_AUTH_RANDOM seed on the create_webserver).
//
// Note: std::random_device's fallback behaviour when no hardware/OS entropy
// source is available is implementation-defined -- it may fall back to a
// deterministic PRNG on some platforms/standard libraries. That does not
// weaken security here: the RFC 7616 opaque is an identifier (not a
// secret), and the anti-guessing guarantee for digest auth comes from
// libmicrohttpd's HMAC-keyed nonce (MHD_OPTION_DIGEST_AUTH_RANDOM), not
// from this value.
#ifdef HAVE_DAUTH
static std::string generate_random_hex_opaque_() {
    std::random_device rd;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 8; ++i) {
        uint32_t sample = static_cast<uint32_t>(rd());
        // Pack 4 bytes -> 8 hex chars per std::random_device sample.
        for (int b = 0; b < 4; ++b) {
            uint32_t octet = (sample >> (b * 8)) & 0xFFu;
            out.push_back(kHex[(octet >> 4) & 0xFu]);
            out.push_back(kHex[octet & 0xFu]);
        }
    }
    return out;
}
#endif  // HAVE_DAUTH

webserver_impl::webserver_impl(webserver* parent, MHD_socket bind_socket_val)
    : parent(parent), daemon_(this, bind_socket_val),
#ifdef HAVE_WEBSOCKET
      // Declared with ws_ (before the services block), so it must be
      // initialised before errors_ here to satisfy -Wreorder.
      ws_upgrader_(ws_),
#endif  // HAVE_WEBSOCKET
      errors_(parent->config), hooks_dispatch_(hooks_, parent->config),
      response_mat_(errors_, hooks_dispatch_, digest_opaque_, parent->config),
      upload_(parent->config),
      dispatcher_(routes_, hooks_dispatch_, errors_, response_mat_,
#ifdef HAVE_WEBSOCKET
                  ws_upgrader_,
#endif  // HAVE_WEBSOCKET
                  parent->config),
      pipeline_(hooks_dispatch_, dispatcher_, parent->config) {
    // Guard against null parent: the dispatch helpers (not_found_page,
    // method_not_allowed_page, internal_error_page, etc.) read the const
    // config bag on `parent` and will dereference this pointer on every
    // request. The only valid call site is webserver::webserver, which
    // always passes `this` — a non-null pointer to the owning webserver.
    // (daemon_ was already constructed with owner=this above; it stores the
    // pointer but never dereferences parent config until start(). The
    // config-reading behavior services below -- errors_ and the ones added
    // in later DR-014 steps -- DO bind parent->config in this init list, so
    // a non-null parent is a hard construction precondition; the sole caller
    // webserver::webserver satisfies it by passing `this`.)
    if (parent == nullptr) {
        throw std::invalid_argument(
            "webserver_impl requires a non-null owning webserver pointer");
    }
#ifdef HAVE_DAUTH
    digest_opaque_ = generate_random_hex_opaque_();
#endif  // HAVE_DAUTH
}

webserver_impl::~webserver_impl() {
#if defined(HAVE_GNUTLS) && defined(MHD_OPTION_HTTPS_CERT_CALLBACK)
    // Clean up cached SNI credentials
    for (auto& [name, creds] : sni_credentials_cache) {
        gnutls_certificate_free_credentials(creds);
    }
    sni_credentials_cache.clear();
#endif  // HAVE_GNUTLS && MHD_OPTION_HTTPS_CERT_CALLBACK
}

// ----- allowed-method serialization (test seam) --------------------------

std::string webserver_impl::serialize_allow_methods(method_set allowed) const {
    return detail::format_allow_header(allowed);
}

// ----- on_*/route lambda-shim registration POLICY ------------------------

// Caller must hold routes_.lock_for_write() (unique_lock). The shim returned
// here is the SAME object that subsequent helpers (commit_handlers_to_shim,
// upsert_v2_table_entry_locked_) mutate; holding the lock across the
// whole prepare->commit->upsert sequence prevents a concurrent registration
// from racing in between.
//
// The returned bool (written /*fresh=*/ below) is true iff the shim was
// newly constructed because no entry existed at this path. on_methods_
// receives it as `is_new_entry` and forwards it unchanged as the `fresh`
// parameter of upsert_v2_table_entry_locked_ -- all three names denote
// the same flag. The v2 route-table conflict probe + table mutation
// (find_v2_entry_by_path_ / upsert_v2_table_entry_locked_ and the reject_*
// guards) live in the route_table collaborator (src/detail/route_table.cpp);
// this reaches them through impl's routes_ member.
std::pair<std::shared_ptr<detail::lambda_resource>, bool>
webserver_impl::prepare_or_create_lambda_shim(const detail::http_endpoint& idx,
                                              method_set methods) {
    const detail::route_entry* existing = routes_.find_v2_entry_by_path_(idx);
    if (existing == nullptr) {
        return {std::make_shared<detail::lambda_resource>(), /*fresh=*/true};
    }
    // Existing entry. route_entry::handler is a
    // shared_ptr<http_resource>. Dynamic-cast to
    // lambda_resource: if the cast misses, a class-based
    // register_path/register_prefix owns this path and we must throw.
    auto shim = std::dynamic_pointer_cast<detail::lambda_resource>(
        existing->handler);
    if (!shim) {
        throw std::invalid_argument(
            "A non-lambda http_resource is already registered at "
            "this path; on_*/route cannot share a path with "
            "register_path/register_prefix");
    }
    // Atomicity pre-check: every requested slot must be empty BEFORE
    // we mutate any of them.
    for_each_requested_method(methods, [&](http_method m) {
        if (shim->has_slot(m)) {
            throw std::invalid_argument(
                "A handler is already registered for one of the "
                "requested methods on this path");
        }
    });
    return {shim, /*fresh=*/false};
}

void webserver_impl::commit_handlers_to_shim(detail::lambda_resource& shim,
        method_set methods,
        std::function<::httpserver::http_response(
            const ::httpserver::http_request&)> handler) {
    // Collect the set of requested methods in iteration order so the
    // loop below can identify the last one. Using a small inline array
    // avoids a heap allocation in the common case (N <= 9 methods).
    //
    // Move-into-last-slot optimisation:
    // all but the last slot receive a copy; the last slot is populated
    // by moving the handler, avoiding one extra heap allocation when the
    // std::function's capture is too large for the SBO buffer.
    std::array<http_method, static_cast<std::size_t>(http_method::count_)> buf{};
    std::size_t count = 0;
    for_each_requested_method(methods, [&](http_method m) {
        buf[count++] = m;
    });
    for (std::size_t i = 0; i < count; ++i) {
        if (i + 1 < count) {
            shim.set_slot(buf[i], handler);          // copy
        } else {
            shim.set_slot(buf[i], std::move(handler));  // move into last slot
        }
    }
}

}  // namespace detail

}  // namespace httpserver
