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
// TASK-034: <microhttpd_ws.h> remains gated (only the upgrade trampolines
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
// TASK-034: feature_unavailable + websocket_handler are public headers
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

// TASK-019: pin httpserver::http::http_utils::cred_type_T values to the
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

namespace detail {

// ----- webserver_impl construction / destruction -------------------------

webserver_impl::webserver_impl(webserver* parent, MHD_socket bind_socket_val)
    : parent(parent), bind_socket(bind_socket_val) {
    // Guard against null parent: the dispatch helpers (not_found_page,
    // method_not_allowed_page, internal_error_page, etc.) read the const
    // config bag on `parent` and will dereference this pointer on every
    // request. The only valid call site is webserver::webserver, which
    // always passes `this` — a non-null pointer to the owning webserver.
    if (parent == nullptr) {
        throw std::invalid_argument(
            "webserver_impl requires a non-null owning webserver pointer");
    }
    pthread_mutex_init(&mutexwait, nullptr);
    pthread_cond_init(&mutexcond, nullptr);
}

webserver_impl::~webserver_impl() {
    pthread_mutex_destroy(&mutexwait);
    pthread_cond_destroy(&mutexcond);

#if defined(HAVE_GNUTLS) && defined(MHD_OPTION_HTTPS_CERT_CALLBACK)
    // Clean up cached SNI credentials
    for (auto& [name, creds] : sni_credentials_cache) {
        gnutls_certificate_free_credentials(creds);
    }
    sni_credentials_cache.clear();
#endif  // HAVE_GNUTLS && MHD_OPTION_HTTPS_CERT_CALLBACK
}

}  // namespace detail

// ----- webserver construction / destruction ------------------------------

webserver::webserver(const create_webserver& params):
    port(params._port),
    start_method(params._start_method),
    max_threads(params._max_threads),
    max_connections(params._max_connections),
    memory_limit(params._memory_limit),
    content_size_limit(params._content_size_limit),
    connection_timeout(params._connection_timeout),
    per_IP_connection_limit(params._per_IP_connection_limit),
    log_access(params._log_access),
    log_error(params._log_error),
    validator(params._validator),
    unescaper(params._unescaper),
    bind_address(params._bind_address),
    bind_address_storage(params._bind_address_storage),
    max_thread_stack_size(params._max_thread_stack_size),
    max_args_count(params._max_args_count),
    max_args_bytes(params._max_args_bytes),
    use_ssl(params._use_ssl),
    use_ipv6(params._use_ipv6),
    use_dual_stack(params._use_dual_stack),
    debug(params._debug),
    pedantic(params._pedantic),
    expose_exception_messages(params._expose_exception_messages),
    expose_credentials_in_logs(params._expose_credentials_in_logs),
    https_mem_key(params._https_mem_key),
    https_mem_cert(params._https_mem_cert),
    https_mem_trust(params._https_mem_trust),
    https_priorities(params._https_priorities),
    cred_type(params._cred_type),
    psk_cred_handler(params._psk_cred_handler),
    digest_auth_random(params._digest_auth_random),
    nonce_nc_size(params._nonce_nc_size),
    default_policy(params._default_policy),
    // TASK-034: stored unconditionally; the ctor body below throws
    // feature_unavailable if this is true on a HAVE_BAUTH-off build.
    basic_auth_enabled(params._basic_auth_enabled),
    digest_auth_enabled(params._digest_auth_enabled),
    regex_checking(params._regex_checking),
    ban_system_enabled(params._ban_system_enabled),
    post_process_enabled(params._post_process_enabled),
    put_processed_data_to_content(params._put_processed_data_to_content),
    file_upload_target(params._file_upload_target),
    file_upload_dir(params._file_upload_dir),
    generate_random_filename_on_upload(params._generate_random_filename_on_upload),
    deferred_enabled(params._deferred_enabled),
    single_resource(params._single_resource),
    tcp_nodelay(params._tcp_nodelay),
    not_found_handler(params._not_found_handler),
    method_not_allowed_handler(params._method_not_allowed_handler),
    internal_error_handler(params._internal_error_handler),
    file_cleanup_callback(params._file_cleanup_callback),
    auth_handler(params._auth_handler),
    auth_skip_paths(params._auth_skip_paths),
    auth_skip_paths_normalized(
        detail::normalize_auth_skip_paths(params._auth_skip_paths)),
    sni_callback(params._sni_callback),
    no_listen_socket(params._no_listen_socket),
    no_thread_safety(params._no_thread_safety),
    turbo(params._turbo),
    suppress_date_header(params._suppress_date_header),
    listen_backlog(params._listen_backlog),
    address_reuse(params._address_reuse),
    connection_memory_increment(params._connection_memory_increment),
    tcp_fastopen_queue_size(params._tcp_fastopen_queue_size),
    sigpipe_handled_by_app(params._sigpipe_handled_by_app),
    https_mem_dhparams(params._https_mem_dhparams),
    https_key_password(params._https_key_password),
    https_priorities_append(params._https_priorities_append),
    no_alpn(params._no_alpn),
    client_discipline_level(params._client_discipline_level),
    // create_webserver uses int=0 as "no pre-bound socket" to keep the
    // public builder header free of <microhttpd.h>. Convert to the
    // MHD_socket sentinel (MHD_INVALID_SOCKET) so the impl always uses
    // a well-defined sentinel. Pass through the impl_ constructor so the
    // impl is fully initialised from the member-initialiser list with no
    // post-construction mutations of impl_ members.
    impl_(std::make_unique<detail::webserver_impl>(
        this,
        (params._bind_socket != 0)
            ? static_cast<MHD_socket>(params._bind_socket)
            : MHD_INVALID_SOCKET)) {
        // TASK-034 §7: any feature the builder asked for that the
        // library was not compiled with must fail loudly here. Throwing
        // from the ctor body (after the member-initialiser list) lets
        // the just-constructed impl_ unique_ptr destroy itself cleanly
        // — no MHD daemon is running yet.
#ifndef HAVE_GNUTLS
        if (use_ssl) {
            throw feature_unavailable("tls", "HAVE_GNUTLS");
        }
#endif
#ifndef HAVE_BAUTH
        if (basic_auth_enabled) {
            throw feature_unavailable("basic_auth", "HAVE_BAUTH");
        }
#endif
#ifndef HAVE_DAUTH
        // security-reviewer-iter1-1 / CWE-287: symmetric guard for digest
        // auth. Without this a HAVE_DAUTH-off build silently accepts
        // digest_auth_enabled=true and the request handler returns
        // WRONG_HEADER, making the authentication gate a silent no-op.
        if (digest_auth_enabled) {
            throw feature_unavailable("digest_auth", "HAVE_DAUTH");
        }
#endif
        ignore_sigpipe();
        // TASK-048: register the three v1 setter aliases as hooks
        // (route_resolved for not_found_handler; before_handler for
        // method_not_allowed_handler and auth_handler). Conditional on
        // each setter being non-null so zero-cost-when-unused holds.
        install_default_alias_hooks_();
}

// TASK-034: build-time feature reporting (PRD-FLG-REQ-003). The body
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


// TASK-045 -- Hook bus skeleton.
//
// register_hook_(): single internal funnel used by every add_hook
// overload. Validates the runtime phase tag matches the overload's
// compile-time phase, allocates a fresh slot id, takes the unique
// writer lock, pushes the callable into the per-phase vector, and
// flips the any_hooks_ gate to true. Returns the armed hook_handle.
//
// All eleven public overloads are thin one-liners that pick the right
// per-phase vector member; the validation, locking, slot allocation,
// and gate flip live here exactly once.
namespace {

// register_hook_impl: helper used by every add_hook overload. Lives in
// an anonymous namespace; it would normally call hook_handle's private
// constructor through webserver's friendship, but webserver's friend
// status doesn't extend to a free function -- so we route the
// hook_handle creation through a tiny webserver static helper that
// IS a member and IS a friend of hook_handle (transitively). See
// webserver::make_hook_handle_ below.
template <class Vec, class Fn>
::httpserver::hook_handle register_hook_impl(
        ::httpserver::detail::webserver_impl* impl,
        ::httpserver::hook_phase requested,
        ::httpserver::hook_phase expected,
        Vec& vec,
        Fn fn) {
    if (requested != expected) {
        throw std::invalid_argument(
            std::string("hook phase mismatch: add_hook overload for ")
            + std::string(::httpserver::to_string(expected))
            + " received phase tag "
            + std::string(::httpserver::to_string(requested)));
    }
    if (!fn) {
        throw std::invalid_argument("hook callable must not be empty");
    }
    const std::uint64_t id =
        impl->next_slot_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::unique_lock lock(impl->hook_table_mutex_);
        vec.push_back({id, std::move(fn)});
        // Store under the unique_lock. memory_order_release here is
        // technically redundant because the subsequent mutex unlock also
        // acts as a release fence. It is kept for clarity, but dispatch
        // hot-path readers MUST use memory_order_acquire on the load, and
        // they pair with the MUTEX RELEASE (unlock), not this store. See
        // webserver_impl.hpp comment on any_hooks_ for the full pairing
        // rationale.
        impl->any_hooks_[static_cast<std::size_t>(expected)].store(
            true, std::memory_order_release);
    }
    return ::httpserver::webserver::make_hook_handle_(impl, expected, id);
}

}  // namespace

// TASK-045: tiny static helper that materialises an armed hook_handle.
// hook_handle's constructor is private but webserver is friend; this
// static gives the anonymous-namespace register_hook_impl a way to
// reach into that private surface without making it a friend itself.
hook_handle webserver::make_hook_handle_(detail::webserver_impl* impl,
                                         hook_phase phase,
                                         std::uint64_t slot_id) noexcept {
    return hook_handle{impl, phase, slot_id};
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const connection_open_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::connection_opened,
        impl_->hooks_connection_opened_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const accept_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::accept_decision,
        impl_->hooks_accept_decision_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(request_received_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::request_received,
        impl_->hooks_request_received_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(body_chunk_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::body_chunk,
        impl_->hooks_body_chunk_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const route_resolved_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::route_resolved,
        impl_->hooks_route_resolved_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(before_handler_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::before_handler,
        impl_->hooks_before_handler_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::handler_exception,
        impl_->hooks_handler_exception_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(after_handler_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::after_handler,
        impl_->hooks_after_handler_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const response_sent_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::response_sent,
        impl_->hooks_response_sent_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const request_completed_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::request_completed,
        impl_->hooks_request_completed_, std::move(fn));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const connection_close_ctx&)> fn) {
    return register_hook_impl(impl_.get(), phase,
        hook_phase::connection_closed,
        impl_->hooks_connection_closed_, std::move(fn));
}

}  // namespace httpserver
