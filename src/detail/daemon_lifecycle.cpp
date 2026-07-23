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

// daemon_lifecycle.cpp -- the MHD daemon handle + start/stop threading
// state, and the option-array / start-flag builders that construct the
// daemon. Extracted from webserver_impl so the coordinator no longer owns
// the daemon handle, the blocking-start mutex/cond pair, or the daemon-
// construction machinery. The public webserver::start/stop/is_running/
// run/... methods (webserver_lifecycle.cpp) drive this state; those remain
// on webserver as the orchestration layer and reach the handle + builders
// through impl_->daemon_.

#include "httpserver/detail/daemon_lifecycle.hpp"

#include <microhttpd.h>
#include <pthread.h>

#include <cstdint>
#include <vector>

#include "httpserver/webserver.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif  // HAVE_GNUTLS

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

daemon_lifecycle::daemon_lifecycle(webserver_impl* owner,
                                   MHD_socket bind_socket_val)
    : bind_socket(bind_socket_val), owner_(owner) {
    pthread_mutex_init(&mutexwait, nullptr);
    pthread_cond_init(&mutexcond, nullptr);
}

daemon_lifecycle::~daemon_lifecycle() {
    pthread_mutex_destroy(&mutexwait);
    pthread_cond_destroy(&mutexcond);
}

// Wrap MHD_OptionItem aggregate-init so each push reads uniformly
// across the option-array builders below.
static MHD_OptionItem make_option(enum MHD_OPTION opt, intptr_t val,
                                  void* ptr = nullptr) {
    MHD_OptionItem x = {opt, val, ptr};
    return x;
}

void daemon_lifecycle::add_base_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    webserver* const parent = owner_->parent;
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

void daemon_lifecycle::add_tls_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    webserver* const parent = owner_->parent;
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

void daemon_lifecycle::add_gnutls_mhd_options(std::vector<MHD_OptionItem>& iov) const {
#ifdef HAVE_GNUTLS
    webserver* const parent = owner_->parent;
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

void daemon_lifecycle::add_extended_mhd_options(std::vector<MHD_OptionItem>& iov) const {
    webserver* const parent = owner_->parent;
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

void daemon_lifecycle::add_https_extra_options(std::vector<MHD_OptionItem>& iov) const {
    webserver* const parent = owner_->parent;
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

void daemon_lifecycle::build_mhd_option_array(std::vector<MHD_OptionItem>& iov) const {
    add_base_mhd_options(iov);
    add_tls_mhd_options(iov);
    add_gnutls_mhd_options(iov);
    add_extended_mhd_options(iov);
    add_https_extra_options(iov);
    iov.push_back(make_option(MHD_OPTION_END, 0, nullptr));
}

int daemon_lifecycle::compose_transport_flags() const {
    webserver* const parent = owner_->parent;
    int flags = 0;
    if (parent->config.use_ssl) flags |= MHD_USE_SSL;
    if (parent->config.use_ipv6) flags |= MHD_USE_IPv6;
    if (parent->config.use_dual_stack) flags |= MHD_USE_DUAL_STACK;
    return flags;
}

int daemon_lifecycle::compose_runtime_flags() const {
    webserver* const parent = owner_->parent;
    int flags = 0;
    if (parent->config.debug) flags |= MHD_USE_DEBUG;
    if (parent->config.pedantic) flags |= MHD_USE_PEDANTIC_CHECKS;
    if (parent->config.deferred_enabled) flags |= MHD_USE_SUSPEND_RESUME;
    if (parent->config.no_listen_socket) flags |= MHD_USE_NO_LISTEN_SOCKET;
    if (parent->config.no_thread_safety) flags |= MHD_USE_NO_THREAD_SAFETY;
    if (parent->config.turbo) flags |= MHD_USE_TURBO;
    if (parent->config.suppress_date_header) flags |= MHD_USE_SUPPRESS_DATE_NO_CLOCK;
#ifdef HAVE_WEBSOCKET
    if (!owner_->ws_.empty()) flags |= MHD_ALLOW_UPGRADE;
#endif  // HAVE_WEBSOCKET
    return flags;
}

int daemon_lifecycle::compose_start_flags() const {
    webserver* const parent = owner_->parent;
    int flags = parent->config.start_method;
    flags |= compose_transport_flags();
    flags |= compose_runtime_flags();
#ifdef USE_FASTOPEN
    flags |= MHD_USE_TCP_FASTOPEN;
#endif
    return flags;
}

}  // namespace detail
}  // namespace httpserver
