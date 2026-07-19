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
#include <cassert>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>

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
#include "httpserver/constants.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_method.hpp"
#include "httpserver/detail/dispatch_util.hpp"
#include "httpserver/detail/method_utils.hpp"
#include "httpserver/detail/route_tier.hpp"

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


// ===== webserver_add_hook.cpp (public webserver:: API) ====================

// webserver_add_hook.cpp -- the typed webserver::add_hook overload family
// and webserver::make_hook_handle_. Each overload delegates registration
// (phase-mismatch + empty-callable validation, slot-id alloc, push under
// lock, gate arming) to detail::hook_bus::add and wraps the returned
// slot_id in an armed hook_handle.
//
// Carved out of src/webserver.cpp to keep both translation
// units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh).

// Tiny static helper that materialises an armed hook_handle.
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
    return make_hook_handle_(impl_.get(), hook_phase::connection_opened,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const accept_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::accept_decision,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(request_received_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::request_received,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(body_chunk_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::body_chunk,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const route_resolved_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::route_resolved,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(before_handler_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::before_handler,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::handler_exception,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<hook_action(after_handler_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::after_handler,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const response_sent_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::response_sent,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const request_completed_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::request_completed,
        impl_->hooks_.add(phase, std::move(fn)));
}

hook_handle webserver::add_hook(hook_phase phase,
        std::function<void(const connection_close_ctx&)> fn) {
    return make_hook_handle_(impl_.get(), hook_phase::connection_closed,
        impl_->hooks_.add(phase, std::move(fn)));
}


// ===== webserver_lifecycle.cpp (public webserver:: API) ====================


// The MHD daemon-construction builders (make_option, add_*_mhd_options,
// build_mhd_option_array, compose_*_flags) moved to the daemon_lifecycle
// collaborator (src/detail/daemon_lifecycle.cpp); the webserver:: lifecycle
// methods below reach them through impl_->daemon_.

bool webserver::start(bool blocking) {
    if (config.start_method == http_utils::THREAD_PER_CONNECTION
            && (config.max_threads != 0 || config.max_thread_stack_size != 0)) {
        throw std::invalid_argument(
            "Cannot specify maximum number of threads when using a thread per connection");
    }

    // Fire the one-shot raw-body-dump opt-in SECURITY
    // WARNING before MHD_start_daemon so the operator sees the
    // notice on every fresh process invocation that actually starts
    // accepting traffic. Defined in webserver_body_pipeline.cpp
    // alongside the consumer of the opt-in flag.
    // Maintenance invariant: this call must happen before any request
    // is dispatched, and therefore before
    // debug_dump_request_body_opted_in()'s magic-static is first
    // initialised by the body pipeline. A future refactor that moves
    // work ahead of webserver::start() (or reorders the body pipeline)
    // must preserve or replicate this ordering.
    detail::maybe_warn_debug_dump_request_body(this);

    vector<struct MHD_OptionItem> iov;
    impl_->daemon_.build_mhd_option_array(iov);
    const int start_conf = impl_->daemon_.compose_start_flags();

    struct MHD_Daemon* d = nullptr;
    if (config.bind_address == nullptr) {
        d = MHD_start_daemon(start_conf, config.port, &detail::webserver_impl::policy_callback, this,
                &detail::webserver_impl::answer_to_connection, impl_.get(), MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_END);
    } else {
        // libmicrohttpd ignores the port argument when MHD_OPTION_SOCK_ADDR
        // is supplied; the literal 1 is a placeholder that only needs to be
        // non-zero.
        d = MHD_start_daemon(start_conf, 1, &detail::webserver_impl::policy_callback, this,
                &detail::webserver_impl::answer_to_connection, impl_.get(), MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_SOCK_ADDR, config.bind_address, MHD_OPTION_END);
    }
    // Release store: publish the daemon (+ its bind-port) so a concurrent get_bound_port() on another thread sees it.
    impl_->daemon_.daemon.store(d, std::memory_order_release);
    if (d == nullptr) {
        throw std::invalid_argument("Unable to connect daemon to port: " + std::to_string(config.port));
    }

    impl_->daemon_.running = true;

    if (blocking) {
        pthread_mutex_lock(&impl_->daemon_.mutexwait);
        while (impl_->daemon_.running) {
            pthread_cond_wait(&impl_->daemon_.mutexcond, &impl_->daemon_.mutexwait);
        }
        pthread_mutex_unlock(&impl_->daemon_.mutexwait);
        return true;
    }
    return false;
}

bool webserver::is_running() {
    return impl_->daemon_.running;
}

bool webserver::stop() {
    if (!impl_->daemon_.running) return false;

    pthread_mutex_lock(&impl_->daemon_.mutexwait);
    impl_->daemon_.running = false;
    pthread_cond_signal(&impl_->daemon_.mutexcond);
    pthread_mutex_unlock(&impl_->daemon_.mutexwait);

    MHD_stop_daemon(impl_->daemon_.daemon.load(std::memory_order_acquire));
    // Reset so the daemon != nullptr guards treat it as absent after stop().
    impl_->daemon_.daemon.store(nullptr, std::memory_order_release);

    // Only shut down the pre-bound socket if one was actually provided.
    // MHD_INVALID_SOCKET (-1 on POSIX, INVALID_SOCKET on Windows) is the
    // sentinel written by the webserver_impl constructor when no pre-bound
    // socket was passed via create_webserver().bind_socket(). Without this
    // guard, the unconditional shutdown() call would operate on fd MHD_INVALID_SOCKET
    // which on POSIX could be interpreted as fd -1 (implementation-defined)
    // and previously (before the fix) the sentinel was 0 which would have
    // shut down stdin (fd 0).
    if (impl_->daemon_.bind_socket != MHD_INVALID_SOCKET) {
        shutdown(impl_->daemon_.bind_socket, 2);
    }

    return true;
}

int webserver::quiesce() {
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return -1;
    MHD_socket fd = MHD_quiesce_daemon(d);
    return static_cast<int>(fd);
}

int webserver::get_listen_fd() const {
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return -1;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(d, MHD_DAEMON_INFO_LISTEN_FD);
    if (info == nullptr) return -1;
    return static_cast<int>(info->listen_fd);
}

unsigned int webserver::get_active_connections() const {
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(d, MHD_DAEMON_INFO_CURRENT_CONNECTIONS);
    if (info == nullptr) return 0;
    return info->num_connections;
}

uint16_t webserver::get_bound_port() const {
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(d, MHD_DAEMON_INFO_BIND_PORT);
    if (info == nullptr) return 0;
    return info->port;
}

bool webserver::run() {
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return false;
    return MHD_run(d) == MHD_YES;
}

bool webserver::run_wait(int32_t millisec) {
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return false;
    return MHD_run_wait(d, millisec) == MHD_YES;
}

bool webserver::get_fdset(fd_set* read_fd_set, fd_set* write_fd_set,
                          fd_set* except_fd_set, int* max_fd) {
    // The signature uses typed fd_set* rather than void*,
    // restoring compile-time type safety. The public header pulls in
    // <sys/select.h> / <winsock2.h> directly because fd_set is a typedef
    // and cannot be portably forward-declared.
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return false;
    MHD_socket mhd_max_fd = 0;
    if (MHD_get_fdset(d,
                      read_fd_set,
                      write_fd_set,
                      except_fd_set,
                      &mhd_max_fd) != MHD_YES) {
        return false;
    }
    *max_fd = static_cast<int>(mhd_max_fd);
    return true;
}

bool webserver::get_timeout(uint64_t* timeout) {
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return false;
    MHD_UNSIGNED_LONG_LONG mhd_timeout = 0;
    if (MHD_get_timeout(d, &mhd_timeout) != MHD_YES) {
        return false;
    }
    *timeout = static_cast<uint64_t>(mhd_timeout);
    return true;
}

bool webserver::add_connection(int client_socket, const struct sockaddr* addr, unsigned int addrlen) {
    // The public signature accepts `unsigned int` instead of
    // `socklen_t` so the public header does not need <sys/socket.h>.
    // POSIX guarantees `socklen_t` is an unsigned integer of at least 32
    // bits; `unsigned int` matches on every supported platform.
    // (The sizeof static_assert is at file scope above.)
    struct MHD_Daemon* d = impl_->daemon_.handle();
    if (d == nullptr) return false;
    return MHD_add_connection(d, client_socket, addr,
                              static_cast<socklen_t>(addrlen)) == MHD_YES;
}

// The deny/allow IP setters forward to the ip_access_control collaborator
// that owns the lists and their locks (and the wildcard-precedence insert
// invariant). See httpserver/detail/ip_access_control.hpp.
void webserver::deny_ip(std::string_view ip) {
    impl_->acl_.deny(ip);
}

void webserver::remove_denied_ip(std::string_view ip) {
    impl_->acl_.remove_denied(ip);
}

void webserver::allow_ip(std::string_view ip) {
    impl_->acl_.allow(ip);
}

void webserver::remove_allowed_ip(std::string_view ip) {
    impl_->acl_.remove_allowed(ip);
}


// ===== webserver_register.cpp (public webserver:: API) ====================



// ----- Resource registration --------------------------------------------

// register_path / register_prefix split. Both public methods funnel into
// this private helper, which carries the validation and insertion logic.
// Keeping the work in one place prevents drift between the two registration
// kinds.
// Input-validation guard for register_impl_. Extracted so register_impl_
// stays inside the project-wide CCN gate.
void webserver::validate_register_inputs_(const std::string& resource,
        const std::shared_ptr<http_resource>& res, bool family) const {
    if (res == nullptr) {
        throw std::invalid_argument("The http_resource pointer cannot be null");
    }
    if (config.single_resource && ((resource != "" && resource != "/") || !family)) {
        throw std::invalid_argument(
            "The resource should be '' or '/' and be registered via "
            "register_prefix when using a single_resource server");
    }
    // Lightweight input hygiene (CWE-20): reject paths containing embedded
    // null bytes, matching validate_on_methods_inputs_'s guard for on_*/
    // route. A std::string can hold '\0' but the underlying regex and
    // routing engines treat it as a string terminator, producing silent
    // mismatches. Reject early with a clear diagnostic rather than letting
    // the error surface deep in regex compilation or route matching.
    if (resource.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "Route path must not contain embedded null bytes");
    }
}

void webserver::register_impl_(const std::string& resource,
                               std::shared_ptr<http_resource> res,
                               bool family) {
    validate_register_inputs_(resource, res, family);

    detail::http_endpoint idx(resource, family, true, config.regex_checking);

    // Duplicate detection happens entirely
    // inside register_v2_route, which takes its own unique_lock on
    // route_table_mutex_ and runs reject_terminus_collision (plus a
    // tier-classification probe for the exact tier) BEFORE any mutation.
    // A duplicate at any tier surfaces as std::invalid_argument with the
    // table still in its prior state -- no rollback required.
    //
    // Ownership contract: when register_v2_route throws,
    // the shared_ptr parameter `res` (and any unique_ptr-derived shared_ptr
    // funnelled through the webserver.hpp inline template shim) is destroyed
    // by exception unwinding, cleanly releasing the resource.
    impl_->routes_.register_v2_route(idx, std::move(res), family);
    impl_->routes_.invalidate_route_cache();
}

void webserver::register_path(const std::string& path,
                              std::shared_ptr<http_resource> res) {
    register_impl_(path, std::move(res), /*family=*/false);
}

void webserver::register_prefix(const std::string& path,
                                std::shared_ptr<http_resource> res) {
    register_impl_(path, std::move(res), /*family=*/true);
}

// Erase a single registration of the requested kind (family).
// Each kind keeps a distinct v2-table entry (parameterized routes live in
// the radix tier, regex routes in the regex_routes_ vector, exact routes
// in the exact_routes_ hash map; prefix routes are radix-tier with
// is_prefix=true), so we route by the same classification used at
// registration. The route_table_mutex_ write lock keeps the erasure
// atomic against concurrent dispatch.
void webserver::unregister_impl_(const string& resource, bool family) {
    detail::http_endpoint he(resource, family, true, config.regex_checking);

    // Erase from the v2 3-tier table.
    // Lock ordering: route_table_mutex_ -> route_lru_cache's internal
    // mutex (inside invalidate_route_cache). Table lock released before
    // the LRU cache is cleared, matching register_impl_ / on_methods_.
    {
        auto table_lock = impl_->routes_.lock_for_write();
        const std::string& key = he.get_url_complete();
        if (family) {
            impl_->routes_.remove_param_prefix_locked_(key, /*is_prefix=*/true);
        } else if (!he.get_url_pars().empty()) {
            impl_->routes_.remove_param_prefix_locked_(key, /*is_prefix=*/false);
        } else {
            // Erase from exact tier; also sweep regex tier (url_complete key).
            impl_->routes_.erase_exact_and_regex_locked_(key);
        }
    }
    impl_->routes_.invalidate_route_cache();
}

void webserver::unregister_path(const string& path) {
    unregister_impl_(path, /*family=*/false);
}

void webserver::unregister_prefix(const string& path) {
    unregister_impl_(path, /*family=*/true);
}

void webserver::unregister_resource(const string& resource) {
    // Build the canonical endpoint key once. The family flag does not
    // affect which v2 storage location holds the entry; the
    // url_complete key is the only sweep key.
    detail::http_endpoint he_exact(resource, /*family=*/false, true, config.regex_checking);

    // Erase from the v2 3-tier table. Hold a single write-lock
    // across all four tier sweeps so no request thread can observe a
    // partially-unregistered state (CWE-367 TOCTOU: a prior register_path
    // AND register_prefix on the same path are both cleared atomically).
    {
        auto table_lock = impl_->routes_.lock_for_write();
        const std::string& key = he_exact.get_url_complete();
        // Sweep every tier the key could occupy so a prior register_path AND
        // register_prefix on the same path are both cleared atomically.
        impl_->routes_.erase_exact_and_regex_locked_(key);
        impl_->routes_.remove_param_prefix_locked_(key, /*is_prefix=*/false);
        impl_->routes_.remove_param_prefix_locked_(key, /*is_prefix=*/true);
    }
    // Delegate cache clearing to invalidate_route_cache() matching the
    // pattern used by register_impl_ and on_methods_ (table lock released
    // before cache is cleared).
    impl_->routes_.invalidate_route_cache();
}

// IP-control API: a symmetric, consistently-named surface (replacing
// the v1 ban_ip / unban_ip / allow_ip / disallow_ip quartet):
//   deny_ip / remove_denied_ip   -> the deny list (exception under ACCEPT)
//   allow_ip / remove_allowed_ip -> the allow list (exception under REJECT;
//                                   also overrides a deny entry under ACCEPT)
// See webserver::deny_ip / allow_ip (impls in webserver_lifecycle.cpp) and
// classify_decision (webserver_callbacks_lifecycle.cpp).


// ===== webserver_routes.cpp (public webserver:: API) ====================


// Input-validation guard for on_methods_. Extracted so on_methods_ stays
// inside the project-wide CCN gate.
void webserver::validate_on_methods_inputs_(method_set methods,
        const std::string& path,
        const std::function<http_response(const http_request&)>& handler) const {
    if (methods.empty()) {
        throw std::invalid_argument(
            "route(method_set, ...) requires at least one method bit set");
    }
    if (!handler) {
        throw std::invalid_argument(
            "The handler function passed to on_*/route must be non-empty");
    }
    // Same single-resource constraint as register_path: only "" or "/"
    // is acceptable. register_impl_'s version of this guard
    // (webserver_register.cpp) has an additional `!family` arm; it is
    // deliberately omitted here because on_*/route routes are ALWAYS
    // exact-matched (family=false by design -- prefix matching is only
    // available via register_prefix()), so the arm would always be
    // true. The guard below is therefore complete for lambda routes.
    if (config.single_resource && path != "" && path != "/") {
        throw std::invalid_argument(
            "When using a single_resource server, on_*/route requires "
            "the path to be '' or '/'");
    }
    // Lightweight input hygiene (CWE-20): reject paths containing embedded
    // null bytes. A std::string can hold '\0' but the underlying regex and
    // routing engines treat it as a string terminator, producing silent
    // mismatches. Reject early with a clear diagnostic rather than letting
    // the error surface deep in regex compilation or route matching.
    if (path.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "Route path must not contain embedded null bytes");
    }
    // Hardening: registration is a privileged
    // server-setup call, not a network-reachable path, but an
    // accidentally megabyte-scale path would still be stored in the
    // route table data structures for no benefit. Reject unreasonably
    // long paths early with a clear diagnostic.
    if (path.size() > 8192) {
        throw std::invalid_argument(
            "Route path exceeds maximum length of 8192 bytes");
    }
}

void webserver::on_methods_(method_set methods,
                            const std::string& path,
                            std::function<http_response(const http_request&)> handler) {
    validate_on_methods_inputs_(methods, path, handler);

    detail::http_endpoint idx(path, /*family=*/false,
                              /*registration=*/true, config.regex_checking);

    {
        // Single-locked window across the v2 conflict probe, the
        // per-method atomicity pre-check, the slot writes, and the
        // table mutation. route_table_mutex_ is the only consistency
        // boundary and must cover both the probe and the mutation. An
        // upsert throw (e.g. reject_terminus_collision) leaves the
        // local shim unreferenced and discarded -- no rollback required
        // since the table itself was never touched.
        auto table_lock = impl_->routes_.lock_for_write();
        // is_new_entry is the bool prepare_or_create_lambda_shim
        // returns as /*fresh=*/ and upsert_v2_table_entry_locked_
        // receives as `fresh` -- the same flag under three names.
        auto [shim, is_new_entry] =
            impl_->prepare_or_create_lambda_shim(idx, methods);
        impl_->commit_handlers_to_shim(*shim, methods, std::move(handler));
        impl_->routes_.upsert_v2_table_entry_locked_(idx, methods, shim,
                                                     is_new_entry);
    }
    impl_->routes_.invalidate_route_cache();
}

// The seven named forwarders below are the only place that maps the
// method name to its http_method enum constant. Each is a thin alias
// for on_methods_; all validation and insertion logic lives there.
// on_delete uses http_method::del because `delete` is a C++ keyword;
// the wire token is "DELETE" (see http_method::to_string).
void webserver::on_get(const std::string& path,
                       std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::get), path, std::move(handler));
}

void webserver::on_post(const std::string& path,
                        std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::post), path, std::move(handler));
}

void webserver::on_put(const std::string& path,
                       std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::put), path, std::move(handler));
}

void webserver::on_delete(const std::string& path,
                          std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::del), path, std::move(handler));
}

void webserver::on_patch(const std::string& path,
                         std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::patch), path, std::move(handler));
}

void webserver::on_options(const std::string& path,
                           std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::options), path, std::move(handler));
}

void webserver::on_head(const std::string& path,
                        std::function<http_response(const http_request&)> handler) {
    on_methods_(method_set{}.set(http_method::head), path, std::move(handler));
}

// Generic table-driven entry points. The single-method form
// rejects http_method::count_ explicitly because the public route()
// overload accepts a runtime value (and so the sentinel is reachable);
// the on_* forwarders never pass count_, so the on_methods_ helper
// itself does not guard against it.
void webserver::route(http_method m,
                      const std::string& path,
                      std::function<http_response(const http_request&)> handler) {
    if (m == http_method::count_) {
        throw std::invalid_argument(
            "http_method::count_ is a sentinel and may not be "
            "registered as a route");
    }
    on_methods_(method_set{}.set(m), path, std::move(handler));
}

void webserver::route(method_set methods,
                      const std::string& path,
                      std::function<http_response(const http_request&)> handler) {
    // method_set::set() does not validate its argument; a caller who
    // passes only count_ bits produces a non-empty bitmask (the
    // count_ bit lies outside 0..count_-1) that for_each_requested_method
    // will iterate over zero times, resulting in an empty shim. The
    // on_methods_ empty() guard uses bits==0 and therefore does not catch
    // this edge case. The single-method route(http_method,...) already
    // guards against http_method::count_ explicitly; for the method_set
    // overload we rely on the documented precondition that callers only
    // set valid method bits. No additional sentinel check is added here
    // because method_set is a user-visible type and validating its
    // internal representation would duplicate policy already owned by
    // method_set itself.
    on_methods_(methods, path, std::move(handler));
}

// Canonical smart-pointer overload. The templated unique_ptr
// shim in webserver.hpp constructs a shared_ptr from the unique_ptr and
// forwards here, so this is the single funnel for both ownership shapes.
void webserver::register_ws_resource(const std::string& resource,
                                     std::shared_ptr<websocket_handler> handler) {
#ifdef HAVE_WEBSOCKET
    if (!handler) {
        throw std::invalid_argument("The websocket_handler pointer cannot be null");
    }
    std::string url_key = http_utils::standardize_url(resource);
    if (!impl_->ws_.try_register(std::move(url_key), std::move(handler))) {
        // v1's operator[]-based insert silently overwrote; v2.0
        // surfaces the collision by throwing.
        throw std::invalid_argument(
            "A websocket_handler is already registered at this path");
    }
#else
    // WebSocket compiled out -- fail loudly at the public
    // entry point so callers can catch feature_unavailable.
    (void)resource;
    (void)handler;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}

void webserver::unregister_ws_resource(const std::string& resource) {
#ifdef HAVE_WEBSOCKET
    impl_->ws_.unregister(http_utils::standardize_url(resource));
#else
    (void)resource;
    throw feature_unavailable("websocket", "HAVE_WEBSOCKET");
#endif
}


// ===== webserver_aliases.cpp (public webserver:: API) ====================

namespace {

// Install the internal_error_handler alias into the dedicated
// last-position slot on webserver_impl. Extracted from
// install_default_alias_hooks_ so the added `if` does not push the host
// function over the CCN bar. See webserver_impl::handler_exception_alias_
// for the lifetime contract (write-once-at-construction).
//
// Naming: no trailing underscore -- this is a file-scope free function in
// an anonymous namespace, not a private member function. Matches the
// naming convention of install_log_access_alias (which itself follows
// the same pattern).
//
// Also sets any_hooks_[handler_exception] so the gate is the single source
// of truth even when only the alias is wired. A future caller
// that checks only any_hooks_ (e.g., a stats collector) then observes the
// correct true value without also needing to know about the alias slot.
void install_internal_error_alias(
        detail::webserver_impl* impl,
        internal_error_handler_t user_handler) {
    if (user_handler == nullptr) return;
    // set_handler_exception_alias also arms any_hooks_[handler_exception]
    // so the gate stays the canonical zero-cost fast-check regardless of
    // whether hooks are in the vector or only in the alias slot.
    impl->hooks_.set_handler_exception_alias(
        [user_handler = std::move(user_handler)](
                const handler_exception_ctx& ctx) -> hook_action {
            // mr->request is always non-null at the call site in
            // handle_dispatch_exception; this guard is purely defensive for
            // any future call site that relaxes that invariant. If that
            // invariant is violated in debug builds, assert fires first.
            assert(ctx.request != nullptr &&
                   "handler_exception_ctx::request must be non-null");
            if (ctx.request == nullptr) return hook_action::pass();
            return hook_action::respond_with(
                user_handler(*ctx.request, ctx.message));
        });
}

// SECURITY (CWE-117): sanitize a string_view for use in an access-log line.
// Replace any ASCII control character (< 0x20 or == 0x7F) with '-' to
// prevent a client from injecting additional log lines via embedded newlines
// or carriage-returns in the request path or method. Appends directly to
// `out` rather than returning a heap-allocated copy, avoiding an extra
// std::string allocation on every request.
void append_sanitized(std::string& out, std::string_view sv) {
    for (unsigned char c : sv) {
        out += (c < 0x20 || c == 0x7f) ? '-' : static_cast<char>(c);
    }
}

// Install the log_access alias into the dedicated response_sent
// alias slot on webserver_impl. Extracted from install_default_alias_hooks_
// for the same reason as install_internal_error_alias: keeping the host
// function under the project CCN gate. See webserver_impl::log_access_alias_
// for the lifetime contract.
void install_log_access_alias(
        detail::webserver_impl* impl,
        log_access_ptr user_logger) {
    if (user_logger == nullptr) return;
    impl->hooks_.set_log_access_alias(
        [user_logger = std::move(user_logger)](
                const response_sent_ctx& ctx) {
        if (ctx.request == nullptr) return;
        std::string_view path = ctx.request->get_path();
        std::string_view method = ctx.request->get_method();
        std::string line;
        // Reserve exactly what this request needs (path + " METHOD: " +
        // method) instead of a fixed guess, so paths/methods longer than
        // ~50 bytes don't trigger a reallocation mid-append.
        line.reserve(path.size() + method.size() + 9);
        // Append path and method with control-character sanitization
        // (CWE-117). Append string_view directly to avoid intermediate
        // heap allocations (performance: saves two alloc/dealloc pairs
        // per request vs. the previous std::string(sv) approach).
        append_sanitized(line, path);
        line += " METHOD: ";
        append_sanitized(line, method);
        user_logger(line);
    });
}

}  // namespace

// ----------------------------------------------------------------
// auth_handler -> before_handler (registered first, ahead of the
// method_not_allowed alias, so auth always fires before it).
//
// This is an alias. Calling auth_handler(fn) registers a hook at
// hook_phase::before_handler. Equivalent to
// ws.add_hook(hook_phase::before_handler, ...).
//
// The hook IS the auth enforcement path — it replaces (and removes)
// the former webserver_impl::apply_auth_short_circuit inline call.
// It respects auth_skip_paths via should_skip_auth, calls the user-
// supplied auth_handler callable, and returns
// hook_action::respond_with(*resp) when auth fails, short-circuiting
// the remaining before_handler chain AND dispatch_resource_handler.
//
// IMPORTANT: because before_handler fires from finalize_answer BEFORE
// dispatch_resource_handler is called (see webserver_request.cpp:
// fire_before_handler_gated), the auth hook fires for every request
// that resolves to a registered route (route hit). It does NOT fire
// for 404 paths (found==false), which is the correct semantic: there
// is no resource to authenticate against.
//
// DO NOT remove or replace this hook registration without also
// providing an equivalent enforcement mechanism. The hook IS the
// security boundary; there is no separate apply_auth_short_circuit
// fallback path remaining.
//
// Design note (CWE-200): the route-hit-only
// firing above creates an auth oracle — requests to unregistered paths
// get 404 without auth, while registered paths get 401 if blocked, so
// 401-vs-404 distinguishes registered from unregistered routes.
// Callers needing uniform authentication on all requests (including
// 404s) should add a catch-all fallback route or register a
// not_found_handler that applies equivalent auth logic.
void webserver::install_auth_alias_() {
    if (config.auth_handler == nullptr) return;
    // Capture both the webserver* (for auth_handler callable) and the
    // webserver_impl* (for should_skip_auth, which normalises the path
    // before comparing against auth_skip_paths).
    webserver* ws_ptr = this;
    detail::webserver_impl* impl_ptr = impl_.get();
    // add_hook returns a prvalue hook_handle; .detach() is called
    // directly on it -- std::move() on a prvalue is a no-op and
    // is omitted here.
    add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [ws_ptr, impl_ptr](before_handler_ctx& ctx) -> hook_action {
                if (ctx.request == nullptr) return hook_action::pass();
                // Respect auth_skip_paths: skip auth for listed prefixes.
                // Empty skip-list is the production-typical case — skip
                // the std::string allocation entirely when there is
                // nothing to compare against (should_skip_auth's own
                // empty-list early-out still fires for the non-empty
                // path).
                if (!ws_ptr->auth_skip_paths_normalized.empty()) {
                    if (impl_ptr->should_skip_auth(ctx.request->get_path())) {
                        return hook_action::pass();
                    }
                }
                // Call the user-supplied auth_handler. The return
                // type is std::optional<http_response>. nullopt
                // means "allow"; an engaged optional carries the
                // rejection response. Compared to the v1
                // shared_ptr<http_response> shape this saves the
                // per-authenticated-request control-block allocation
                // (one heap alloc removed per request that runs through
                // this hook), and small responses ride the http_response
                // SBO with zero further allocs.
                // Fail-closed: the auth seat is a security
                // boundary, so a throwing auth callable must NOT fall
                // through to the resource. The generic short-circuit
                // firing path (fire_short_circuit_hooks_for_phase) treats
                // a throwing hook as pass(), which would be a security
                // fail-open here (CWE-703). Wrap the callable in a local
                // try/catch that short-circuits with a 500 instead,
                // matching the documented dispatch contract that an
                // exception thrown by a hook is routed through the same
                // path as a throwing resource handler (which yields 500).
                std::optional<http_response> rejection;
                bool auth_threw = false;
                try {
                    rejection = ws_ptr->config.auth_handler(*ctx.request);
                } catch (const std::exception& e) {
                    detail::log_dispatch_error(ws_ptr->config,
                        std::string("auth_handler threw: ").append(e.what())
                            .append("; failing closed with 500"));
                    auth_threw = true;
                } catch (...) {
                    detail::log_dispatch_error(ws_ptr->config,
                        "auth_handler threw unknown exception; "
                        "failing closed with 500");
                    auth_threw = true;
                }
                if (auth_threw) {
                    // Deliberately ignores create_webserver::expose_exception_
                    // messages: the auth fail-closed 500 always has an empty
                    // body, unlike internal_error_page (webserver_error_pages.cpp),
                    // to keep the auth boundary fail-safe by default.
                    return hook_action::respond_with(
                        http_response::empty().with_status(500));
                }
                if (!rejection) {
                    return hook_action::pass();
                }
                // Auth failed: short-circuit with the rejection response.
                return hook_action::respond_with(std::move(*rejection));
            }))
        .detach();
}

// ----------------------------------------------------------------
// method_not_allowed_handler -> before_handler (registered second).
//
// This is an alias. Calling method_not_allowed_handler(fn) registers
// a hook at hook_phase::before_handler. Equivalent to
// ws.add_hook(hook_phase::before_handler, ...).
//
// The hook checks whether the request method is in the resource's
// allowed set. If not, it calls method_not_allowed_handler (or
// synthesises a default 405 body), appends the Allow header, and
// returns hook_action::respond_with(...) to short-circuit dispatch.
void webserver::install_method_not_allowed_alias_() {
    if (config.method_not_allowed_handler == nullptr) return;
    webserver* ws_ptr = this;
    add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [ws_ptr](before_handler_ctx& ctx) -> hook_action {
                // Only fire when the resource is known (route hit).
                if (ctx.resource == nullptr) {
                    return hook_action::pass();
                }
                if (ctx.resource->is_allowed(ctx.method)) {
                    return hook_action::pass();
                }
                // Method not allowed: build the response.
                http_response resp =
                    ws_ptr->config.method_not_allowed_handler(*ctx.request);
                // Append Allow header from the matched route descriptor.
                if (ctx.matched) {
                    std::string allow_value =
                        detail::format_allow_header(ctx.matched->methods);
                    if (!allow_value.empty()) {
                        resp.with_header(
                            http::http_utils::http_header_allow,
                            allow_value);
                    }
                }
                return hook_action::respond_with(std::move(resp));
            }))
        .detach();
}

// ----------------------------------------------------------------
// not_found_handler -> route_resolved (observation-only phase).
//
// This is an alias. Calling not_found_handler(fn) registers a hook
// at hook_phase::route_resolved. Equivalent to
// ws.add_hook(hook_phase::route_resolved, ...).
//
// Structural pin: route_resolved is observation-only
// — it cannot mutate the in-flight response or its delivery, and
// route_resolved_ctx exposes no mutable response slot. The user-
// provided not_found_handler is therefore consulted at the v1 call
// site webserver_impl::not_found_page (invoked from finalize_answer
// and the materialize-fallback path; see src/detail/webserver_error_pages.cpp
// and src/detail/webserver_request.cpp). The alias seat here is the
// architectural anchor: it reserves a stable hook[0] index at this
// phase, it keeps the hook count observable
// via the public hook API (verified by hooks_alias_count_test), and
// it gives future observation-only integrations (logging, metrics)
// a known phase boundary to subscribe alongside. The on-wire 404
// body shape is pinned by hooks_not_found_alias_test (default and
// custom branches) and by basic.cpp:custom_not_found_handler.
void webserver::install_not_found_alias_() {
    if (config.not_found_handler == nullptr) return;
    add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [](const route_resolved_ctx&) {
                // Pure observation marker. The on-wire 404 body is
                // synthesised by webserver_impl::not_found_page; this
                // hook intentionally does NOT re-invoke the user
                // handler (doing so would double-count the user
                // handler's call rate and violate v1 observed-call-
                // count semantics). The route_resolved phase forbids
                // mutating the response. The unused parameter makes
                // explicit that no response-shaping decision is taken
                // here.
            }))
        .detach();
}

void webserver::install_default_alias_hooks_() {
    install_auth_alias_();
    install_method_not_allowed_alias_();
    install_not_found_alias_();

    // ----------------------------------------------------------------
    // internal_error_handler -> handler_exception alias slot (LAST position).
    //
    // This is an alias. Calling internal_error_handler(fn) on the builder
    // makes the user callable the LAST-position fallback in the
    // handler_exception chain.
    //
    // Unlike auth_handler / method_not_allowed_handler / not_found_handler
    // (which install at the FIRST position via add_hook so they short-
    // circuit before user hooks), the internal_error_handler alias must
    // fire LAST so user-added handler_exception hooks have a chance to
    // recover first. We achieve this by storing the alias in the
    // dedicated webserver_impl::handler_exception_alias_ slot rather than
    // push_back-ing into the hooks_handler_exception_ vector. The fire
    // site (fire_handler_exception in src/hook_handle.cpp) iterates the
    // user vector first and only then invokes the alias slot.
    //
    // The alias body invokes the user-supplied callable with the
    // originating exception's message and returns
    // hook_action::respond_with(response). If the user callable itself
    // throws, fire_handler_exception's catch arm absorbs it and returns
    // nullopt; the caller in dispatch_resource_handler then emits the
    // hardcoded empty-body 500 DIRECTLY without re-invoking the user
    // callable (it has already been seen to throw on this request --
    // calling it a second time would observably invoke the user code
    // twice for one logical exception). See webserver_dispatch.cpp.
    //
    // The alias slot is written exactly once here and is immutable
    // thereafter. Runtime extension of the
    // handler_exception phase is via add_hook(); the alias slot is not
    // user-mutable post-construction.
    install_internal_error_alias(impl_.get(), config.internal_error_handler);

    // ----------------------------------------------------------------
    // log_access -> response_sent alias slot.
    //
    // This is an alias. Calling log_access(fn) on the create_webserver
    // builder wires `fn` into the dedicated single-slot member
    // webserver_impl::log_access_alias_, which fire_response_sent
    // invokes AFTER the user-added response_sent vector. Users who want
    // the structured ctx (status, bytes_queued, elapsed -- the data
    // issues #281 and #69 asked for) should call
    // add_hook(hook_phase::response_sent, ...) directly.
    //
    // Format: '<path> METHOD: <method>' -- mirrors v1 access_log to keep
    // basic.cpp log_access_callback test passing without modification.
    install_log_access_alias(impl_.get(), config.log_access);
}

}  // namespace httpserver

