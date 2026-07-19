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
// <microhttpd_ws.h> remains gated (only the upgrade trampolines
// need it). The public websocket_handler header is unconditional and is
// included below with the other project headers.
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

#include "httpserver/create_webserver.hpp"
// feature_unavailable + websocket_handler are public headers
// included unconditionally so the public surface is identical across
// HAVE_WEBSOCKET-on and HAVE_WEBSOCKET-off builds.
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/path_normalize.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/detail/body.hpp"

#define _REENTRANT 1

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

// Pin httpserver::http::http_utils::cred_type_T values to the
// GnuTLS credentials enum. The cred_type_T enum body in
// src/httpserver/http_utils.hpp hard-codes the integer values rather
// than referencing GNUTLS_CRD_* (which would force the public header to
// drag <gnutls/gnutls.h> through the umbrella). This block is the
// compile-time guard that those hard-coded values stay in lockstep
// with the upstream definitions; an upstream renumber breaks the build
// here, where someone with full context can react.
static_assert(static_cast<int>(::httpserver::http::http_utils::CERTIFICATE) ==
              static_cast<int>(GNUTLS_CRD_CERTIFICATE),
              "cred_type_T::CERTIFICATE drifted from GNUTLS_CRD_CERTIFICATE");
static_assert(static_cast<int>(::httpserver::http::http_utils::ANON) ==
              static_cast<int>(GNUTLS_CRD_ANON),
              "cred_type_T::ANON drifted from GNUTLS_CRD_ANON");
static_assert(static_cast<int>(::httpserver::http::http_utils::SRP) ==
              static_cast<int>(GNUTLS_CRD_SRP),
              "cred_type_T::SRP drifted from GNUTLS_CRD_SRP");
static_assert(static_cast<int>(::httpserver::http::http_utils::PSK) ==
              static_cast<int>(GNUTLS_CRD_PSK),
              "cred_type_T::PSK drifted from GNUTLS_CRD_PSK");
static_assert(static_cast<int>(::httpserver::http::http_utils::IA) ==
              static_cast<int>(GNUTLS_CRD_IA),
              "cred_type_T::IA drifted from GNUTLS_CRD_IA");
#endif  // HAVE_GNUTLS

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

using std::string;
using std::pair;
using std::vector;
using std::map;
using std::set;

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;

namespace httpserver {

#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
static void catcher(int) { }
#endif

static void ignore_sigpipe() {
// Mingw doesn't implement SIGPIPE
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
    struct sigaction oldsig;
    struct sigaction sig;

    sig.sa_handler = &catcher;
    sigemptyset(&sig.sa_mask);
#ifdef SA_INTERRUPT
    sig.sa_flags = SA_INTERRUPT;  /* SunOS */
#else  // SA_INTERRUPT
    sig.sa_flags = SA_RESTART;
#endif  // SA_INTERRUPTT
    if (0 != sigaction(SIGPIPE, &sig, &oldsig)) {
        fprintf(stderr, "Failed to install SIGPIPE handler: %s\n", strerror(errno));
    }
#endif
}

// webserver_impl construction / destruction and the small non-trampoline
// webserver_impl member glue (serialize_allow_methods, the on_*/route
// lambda-shim helpers) live in src/detail/webserver_impl.cpp. This TU holds
// only the public webserver:: façade core.

// ----- webserver construction / destruction ------------------------------

webserver::webserver(const create_webserver& params):
    // Copy the builder's config wholesale; the feature-availability guards
    // in the ctor body below throw if an option (e.g. basic_auth_enabled)
    // was requested on a build that lacks it.
    config(params._config),
    // Derived at construction from config.auth_skip_paths (not a builder
    // input), so it is initialised separately rather than copied.
    auth_skip_paths_normalized(
        detail::normalize_auth_skip_paths(params._config.auth_skip_paths)),
    // create_webserver uses int=0 as "no pre-bound socket" to keep the
    // public builder header free of <microhttpd.h>. Convert to the
    // MHD_socket sentinel (MHD_INVALID_SOCKET) so the impl always uses
    // a well-defined sentinel. Pass through the impl_ constructor so the
    // impl is fully initialised from the member-initialiser list with no
    // post-construction mutations of impl_ members.
    impl_(std::make_unique<detail::webserver_impl>(
        this,
        (params._config.bind_socket != 0)
            ? static_cast<MHD_socket>(params._config.bind_socket)
            : MHD_INVALID_SOCKET)) {
        // Any feature the builder asked for that the
        // library was not compiled with must fail loudly here. Throwing
        // from the ctor body (after the member-initialiser list) lets
        // the just-constructed impl_ unique_ptr destroy itself cleanly
        // — no MHD daemon is running yet.
#ifndef HAVE_GNUTLS
        if (config.use_ssl) {
            throw feature_unavailable("tls", "HAVE_GNUTLS");
        }
#endif
#ifndef HAVE_BAUTH
        if (config.basic_auth_enabled) {
            throw feature_unavailable("basic_auth", "HAVE_BAUTH");
        }
#endif
#ifndef HAVE_DAUTH
        // CWE-287: symmetric guard for digest
        // auth. Without this a HAVE_DAUTH-off build silently accepts
        // digest_auth_enabled=true and the request handler returns
        // WRONG_HEADER, making the authentication gate a silent no-op.
        if (config.digest_auth_enabled) {
            throw feature_unavailable("digest_auth", "HAVE_DAUTH");
        }
#endif
        ignore_sigpipe();
        // Register the three v1 setter aliases as hooks
        // (route_resolved for not_found_handler; before_handler for
        // method_not_allowed_handler and auth_handler). Conditional on
        // each setter being non-null so zero-cost-when-unused holds.
        install_default_alias_hooks_();
}

// Build-time feature reporting. The body
// lives in this TU rather than in the header so consumers see whatever
// HAVE_* the library was built with — not whatever HAVE_* their own TU
// happens to define.
//
// The struct-tag spelling `struct webserver::features` disambiguates the
// return type from the function name (both spelled `features`). The
// returned aggregate uses the same elaborated form for the same reason.
struct webserver::features webserver::features() noexcept {
    // Constexpr locals resolve each HAVE_* flag once. The aggregate-init
    // braces below omit the type name to avoid the name collision between
    // the return type and the member function (both spelled `features`);
    // `return {a,b,c,d};` is resolved via the elaborated-type-specifier
    // in the function signature above.
#ifdef HAVE_BAUTH
    constexpr bool k_bauth = true;
#else
    constexpr bool k_bauth = false;
#endif
#ifdef HAVE_DAUTH
    constexpr bool k_dauth = true;
#else
    constexpr bool k_dauth = false;
#endif
#ifdef HAVE_GNUTLS
    constexpr bool k_tls = true;
#else
    constexpr bool k_tls = false;
#endif
#ifdef HAVE_WEBSOCKET
    constexpr bool k_ws = true;
#else
    constexpr bool k_ws = false;
#endif
    return {k_bauth, k_dauth, k_tls, k_ws};
}

webserver::~webserver() {
    stop();
    // impl_'s destructor (running pthread destroys + GnuTLS cleanup) runs
    // when the unique_ptr is destroyed, after this body finishes.
}

void webserver::stop_and_wait() {
    // The "wait for in-flight handlers" guarantee is provided by
    // MHD_stop_daemon(), which is a blocking call that drains all active
    // connections and joins libmicrohttpd's worker threads before returning.
    // stop() calls MHD_stop_daemon() internally, so this wrapper fulfils its
    // stronger contract without additional synchronisation.  If the contract
    // were ever stronger than what MHD_stop_daemon() provides (e.g. an
    // application-level quiesce step), the extra logic should be added here
    // rather than in stop(), preserving the distinction between the two
    // entry-points.
    stop();
}

}  // namespace httpserver
