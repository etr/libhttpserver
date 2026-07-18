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

// Pin add_connection's unsigned-int/socklen_t ABI contract at file
// scope so the assertion is evaluated once at TU start rather than
// being associated with the specific function body. socklen_t is a 32-bit
// integer on every supported platform (unsigned on POSIX, signed `int` on
// Windows winsock2). `unsigned int` is at least as wide as either, so the
// static_cast<socklen_t>(addrlen) in add_connection is value-preserving for
// any realistic sockaddr length (a few hundred bytes at most).
static_assert(sizeof(unsigned int) >= sizeof(socklen_t),
              "unsigned int is narrower than socklen_t on this platform; "
              "webserver::add_connection's public signature must be widened.");

using std::string;
using std::pair;
using std::vector;
using std::map;
using std::set;

namespace httpserver {

using httpserver::http::http_utils;
using httpserver::http::ip_representation;
using httpserver::http::base_unescaper;

namespace detail {

// Wrap MHD_OptionItem aggregate-init so each push reads uniformly
// across the option-array builders below. Replaces the local
// struct-with-operator() that used to live inside webserver::start().
static MHD_OptionItem make_option(enum MHD_OPTION opt, intptr_t val,
                                  void* ptr = nullptr) {
    MHD_OptionItem x = {opt, val, ptr};
    return x;
}

void webserver_impl::add_base_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    iov.push_back(make_option(MHD_OPTION_NOTIFY_COMPLETED,
                              (intptr_t) &webserver_impl::request_completed, nullptr));
    // Per-connection arena anchor. MHD_OPTION_NOTIFY_CONNECTION
    // hands us a per-connection void** (socket_context) on STARTED, where
    // we new a detail::connection_state (which owns the arena), and on
    // CLOSED, where we delete it. This makes the arena's lifetime equal
    // to the MHD_Connection's lifetime; request_completed reuses the
    // arena across keep-alive request boundaries via arena_.release().
    // The closure pointer is the owning webserver* so the callback can
    // reach impl_->hooks_ (has_hooks_for) / fire_connection_opened /
    // fire_connection_closed.
    iov.push_back(make_option(MHD_OPTION_NOTIFY_CONNECTION,
                              (intptr_t) &webserver_impl::connection_notify, parent));
    iov.push_back(make_option(MHD_OPTION_URI_LOG_CALLBACK,
                              (intptr_t) &webserver_impl::uri_log, parent));
    iov.push_back(make_option(MHD_OPTION_EXTERNAL_LOGGER,
                              (intptr_t) &webserver_impl::error_log, parent));
    iov.push_back(make_option(MHD_OPTION_UNESCAPE_CALLBACK,
                              (intptr_t) &webserver_impl::unescaper_func, parent));
    iov.push_back(make_option(MHD_OPTION_CONNECTION_TIMEOUT, parent->config.connection_timeout));
    if (bind_socket != MHD_INVALID_SOCKET) {
        iov.push_back(make_option(MHD_OPTION_LISTEN_SOCKET, bind_socket));
    }
    if (parent->config.max_threads != 0) {
        iov.push_back(make_option(MHD_OPTION_THREAD_POOL_SIZE, parent->config.max_threads));
    }
    if (parent->config.max_connections != 0) {
        iov.push_back(make_option(MHD_OPTION_CONNECTION_LIMIT, parent->config.max_connections));
    }
    if (parent->config.memory_limit != 0) {
        iov.push_back(make_option(MHD_OPTION_CONNECTION_MEMORY_LIMIT, parent->config.memory_limit));
    }
    if (parent->config.per_IP_connection_limit != 0) {
        iov.push_back(make_option(MHD_OPTION_PER_IP_CONNECTION_LIMIT, parent->config.per_IP_connection_limit));
    }
    if (parent->config.max_thread_stack_size != 0) {
        iov.push_back(make_option(MHD_OPTION_THREAD_STACK_SIZE, parent->config.max_thread_stack_size));
    }
#ifdef HAVE_DAUTH
    if (parent->config.nonce_nc_size != 0) {
        iov.push_back(make_option(MHD_OPTION_NONCE_NC_SIZE, parent->config.nonce_nc_size));
    }
#endif  // HAVE_DAUTH
}

void webserver_impl::add_tls_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    if (parent->config.use_ssl) {
        // const_cast respects the MHD C interface, which takes a void*
        // even though the data is read-only at the library boundary.
        iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_KEY, 0,
                                  reinterpret_cast<void*>(const_cast<char*>(parent->config.https_mem_key.c_str()))));
        iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_CERT, 0,
                                  reinterpret_cast<void*>(const_cast<char*>(parent->config.https_mem_cert.c_str()))));
        if (!parent->config.https_mem_trust.empty()) {
            iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_TRUST, 0,
                                      reinterpret_cast<void*>(const_cast<char*>(parent->config.https_mem_trust.c_str()))));
        }
        if (!parent->config.https_priorities.empty()) {
            iov.push_back(make_option(MHD_OPTION_HTTPS_PRIORITIES, 0,
                                      reinterpret_cast<void*>(const_cast<char*>(parent->config.https_priorities.c_str()))));
        }
    }
#ifdef HAVE_DAUTH
    if (parent->config.digest_auth_random != "") {
        iov.push_back(make_option(MHD_OPTION_DIGEST_AUTH_RANDOM,
                                  parent->config.digest_auth_random.size(),
                                  const_cast<char*>(parent->config.digest_auth_random.c_str())));
    }
#endif  // HAVE_DAUTH
}

void webserver_impl::add_gnutls_mhd_options(std::vector<MHD_OptionItem>& iov) const {
#ifdef HAVE_GNUTLS
    if (parent->config.cred_type != http_utils::NONE) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_CRED_TYPE, parent->config.cred_type));
    }
    if (parent->config.psk_cred_handler != nullptr && parent->config.use_ssl) {
        iov.push_back(make_option(MHD_OPTION_GNUTLS_PSK_CRED_HANDLER,
                                  (intptr_t)&webserver_impl::psk_cred_handler_func, parent));
    }
#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
    if (parent->config.sni_callback != nullptr && parent->config.use_ssl) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_CERT_CALLBACK,
                                  (intptr_t)&webserver_impl::sni_cert_callback_func, parent));
    }
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#else   // HAVE_GNUTLS
    (void)iov;
#endif  // HAVE_GNUTLS
}

void webserver_impl::add_extended_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    if (parent->config.listen_backlog > 0) {
        iov.push_back(make_option(MHD_OPTION_LISTEN_BACKLOG_SIZE, parent->config.listen_backlog));
    }
    if (parent->config.address_reuse != 0) {
        iov.push_back(make_option(MHD_OPTION_LISTENING_ADDRESS_REUSE, parent->config.address_reuse));
    }
    if (parent->config.connection_memory_increment > 0) {
        iov.push_back(make_option(MHD_OPTION_CONNECTION_MEMORY_INCREMENT,
                                  parent->config.connection_memory_increment));
    }
    if (parent->config.tcp_fastopen_queue_size > 0) {
        iov.push_back(make_option(MHD_OPTION_TCP_FASTOPEN_QUEUE_SIZE,
                                  parent->config.tcp_fastopen_queue_size));
    }
    if (parent->config.sigpipe_handled_by_app) {
        iov.push_back(make_option(MHD_OPTION_SIGPIPE_HANDLED_BY_APP, 1));
    }
    if (parent->config.no_alpn) {
        iov.push_back(make_option(MHD_OPTION_TLS_NO_ALPN, 1));
    }
    if (parent->config.client_discipline_level >= 0) {
        iov.push_back(make_option(MHD_OPTION_CLIENT_DISCIPLINE_LVL, parent->config.client_discipline_level));
    }
}

void webserver_impl::add_https_extra_options(std::vector<MHD_OptionItem>& iov) const {
    if (!parent->config.https_mem_dhparams.empty()) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_MEM_DHPARAMS, 0,
                                  const_cast<char*>(parent->config.https_mem_dhparams.c_str())));
    }
    if (!parent->config.https_key_password.empty()) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_KEY_PASSWORD, 0,
                                  const_cast<char*>(parent->config.https_key_password.c_str())));
    }
    if (!parent->config.https_priorities_append.empty()) {
        iov.push_back(make_option(MHD_OPTION_HTTPS_PRIORITIES_APPEND, 0,
                                  const_cast<char*>(parent->config.https_priorities_append.c_str())));
    }
}

void webserver_impl::build_mhd_option_array(std::vector<MHD_OptionItem>& iov) const {
    add_base_mhd_options(iov);
    add_tls_mhd_options(iov);
    add_gnutls_mhd_options(iov);
    add_extended_mhd_options(iov);
    add_https_extra_options(iov);
    iov.push_back(make_option(MHD_OPTION_END, 0, nullptr));
}

int webserver_impl::compose_transport_flags() const {
    int flags = 0;
    if (parent->config.use_ssl) flags |= MHD_USE_SSL;
    if (parent->config.use_ipv6) flags |= MHD_USE_IPv6;
    if (parent->config.use_dual_stack) flags |= MHD_USE_DUAL_STACK;
    return flags;
}

int webserver_impl::compose_runtime_flags() const {
    int flags = 0;
    if (parent->config.debug) flags |= MHD_USE_DEBUG;
    if (parent->config.pedantic) flags |= MHD_USE_PEDANTIC_CHECKS;
    if (parent->config.deferred_enabled) flags |= MHD_USE_SUSPEND_RESUME;
    if (parent->config.no_listen_socket) flags |= MHD_USE_NO_LISTEN_SOCKET;
    if (parent->config.no_thread_safety) flags |= MHD_USE_NO_THREAD_SAFETY;
    if (parent->config.turbo) flags |= MHD_USE_TURBO;
    if (parent->config.suppress_date_header) flags |= MHD_USE_SUPPRESS_DATE_NO_CLOCK;
#ifdef HAVE_WEBSOCKET
    if (!ws_.empty()) flags |= MHD_ALLOW_UPGRADE;
#endif  // HAVE_WEBSOCKET
    return flags;
}

int webserver_impl::compose_start_flags() const {
    int flags = parent->config.start_method;
    flags |= compose_transport_flags();
    flags |= compose_runtime_flags();
#ifdef USE_FASTOPEN
    flags |= MHD_USE_TCP_FASTOPEN;
#endif
    return flags;
}

}  // namespace detail
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
    impl_->build_mhd_option_array(iov);
    const int start_conf = impl_->compose_start_flags();

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
    impl_->daemon.store(d, std::memory_order_release);
    if (d == nullptr) {
        throw std::invalid_argument("Unable to connect daemon to port: " + std::to_string(config.port));
    }

    impl_->running = true;

    if (blocking) {
        pthread_mutex_lock(&impl_->mutexwait);
        while (impl_->running) {
            pthread_cond_wait(&impl_->mutexcond, &impl_->mutexwait);
        }
        pthread_mutex_unlock(&impl_->mutexwait);
        return true;
    }
    return false;
}

bool webserver::is_running() {
    return impl_->running;
}

bool webserver::stop() {
    if (!impl_->running) return false;

    pthread_mutex_lock(&impl_->mutexwait);
    impl_->running = false;
    pthread_cond_signal(&impl_->mutexcond);
    pthread_mutex_unlock(&impl_->mutexwait);

    MHD_stop_daemon(impl_->daemon.load(std::memory_order_acquire));
    // Reset so the daemon != nullptr guards treat it as absent after stop().
    impl_->daemon.store(nullptr, std::memory_order_release);

    // Only shut down the pre-bound socket if one was actually provided.
    // MHD_INVALID_SOCKET (-1 on POSIX, INVALID_SOCKET on Windows) is the
    // sentinel written by the webserver_impl constructor when no pre-bound
    // socket was passed via create_webserver().bind_socket(). Without this
    // guard, the unconditional shutdown() call would operate on fd MHD_INVALID_SOCKET
    // which on POSIX could be interpreted as fd -1 (implementation-defined)
    // and previously (before the fix) the sentinel was 0 which would have
    // shut down stdin (fd 0).
    if (impl_->bind_socket != MHD_INVALID_SOCKET) {
        shutdown(impl_->bind_socket, 2);
    }

    return true;
}

int webserver::quiesce() {
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
    if (d == nullptr) return -1;
    MHD_socket fd = MHD_quiesce_daemon(d);
    return static_cast<int>(fd);
}

int webserver::get_listen_fd() const {
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
    if (d == nullptr) return -1;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(d, MHD_DAEMON_INFO_LISTEN_FD);
    if (info == nullptr) return -1;
    return static_cast<int>(info->listen_fd);
}

unsigned int webserver::get_active_connections() const {
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
    if (d == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(d, MHD_DAEMON_INFO_CURRENT_CONNECTIONS);
    if (info == nullptr) return 0;
    return info->num_connections;
}

uint16_t webserver::get_bound_port() const {
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
    if (d == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(d, MHD_DAEMON_INFO_BIND_PORT);
    if (info == nullptr) return 0;
    return info->port;
}

bool webserver::run() {
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
    if (d == nullptr) return false;
    return MHD_run(d) == MHD_YES;
}

bool webserver::run_wait(int32_t millisec) {
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
    if (d == nullptr) return false;
    return MHD_run_wait(d, millisec) == MHD_YES;
}

bool webserver::get_fdset(fd_set* read_fd_set, fd_set* write_fd_set,
                          fd_set* except_fd_set, int* max_fd) {
    // The signature uses typed fd_set* rather than void*,
    // restoring compile-time type safety. The public header pulls in
    // <sys/select.h> / <winsock2.h> directly because fd_set is a typedef
    // and cannot be portably forward-declared.
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
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
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
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
    struct MHD_Daemon* d = impl_->daemon.load(std::memory_order_acquire);
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

}  // namespace httpserver
