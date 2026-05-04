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

// TASK-014: webserver PIMPL backing class.
//
// This header is *internal*. It is reachable only when compiling the
// libhttpserver translation units themselves (HTTPSERVER_COMPILATION
// is supplied through src/Makefile.am AM_CPPFLAGS). It is NOT included
// from the public umbrella <httpserver.hpp>, so the gate is the strict
// one-mode form, not the dual-mode form used by other detail headers.
#if !defined(HTTPSERVER_COMPILATION)
#error "webserver_impl.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_

#include <microhttpd.h>
#include <pthread.h>
#include <stdarg.h>

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif  // HAVE_GNUTLS

#include "httpserver/http_utils.hpp"
#include "httpserver/detail/http_endpoint.hpp"

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

class webserver;
class http_resource;
class http_response;
#ifdef HAVE_WEBSOCKET
class websocket_handler;
#endif  // HAVE_WEBSOCKET

namespace detail {

struct modded_request;

// connection_state: per-MHD_Connection arena anchor.
//
// Defined as a near-empty type so downstream tasks (TASK-016) can add
// members (e.g. std::pmr::monotonic_buffer_resource arena_) without
// retouching the public header. Copy/move are deleted now so adding
// non-copyable/non-movable members later does not change the trait.
struct connection_state {
    connection_state() = default;
    connection_state(const connection_state&) = delete;
    connection_state& operator=(const connection_state&) = delete;
    connection_state(connection_state&&) = delete;
    connection_state& operator=(connection_state&&) = delete;
};

// webserver_impl: backing object holding all backend-coupled state of
// `webserver` (MHD daemon, mutexes, ban/allowance sets, route table,
// route cache, websocket registry, optional GnuTLS SNI cache) plus the
// dispatch helpers and MHD trampolines that operate on those.
//
// Members are deliberately public: webserver and the free-function MHD
// callbacks all need direct access. The boundary that matters is between
// the public header and this internal class -- not between webserver and
// its own impl.
class webserver_impl {
 public:
    explicit webserver_impl(webserver* parent);
    ~webserver_impl();
    webserver_impl(const webserver_impl&) = delete;
    webserver_impl& operator=(const webserver_impl&) = delete;
    webserver_impl(webserver_impl&&) = delete;
    webserver_impl& operator=(webserver_impl&&) = delete;

    // Back-pointer used by the dispatch helpers to read the const config
    // bag still living on `webserver` (port, max_threads, certs, etc.).
    // Set in the constructor to the owning webserver.
    webserver* parent = nullptr;

    struct MHD_Daemon* daemon = nullptr;
    MHD_socket bind_socket = 0;

    pthread_mutex_t mutexwait;
    pthread_cond_t  mutexcond;

    bool running = false;

    std::shared_mutex registered_resources_mutex;
    std::map<detail::http_endpoint, ::httpserver::http_resource*> registered_resources;
    std::map<std::string, ::httpserver::http_resource*>           registered_resources_str;
    std::map<detail::http_endpoint, ::httpserver::http_resource*> registered_resources_regex;

    struct route_cache_entry {
        detail::http_endpoint matched_endpoint;
        ::httpserver::http_resource* resource;
    };
    static constexpr size_t ROUTE_CACHE_MAX_SIZE = 256;
    std::mutex route_cache_mutex;
    std::list<std::pair<std::string, route_cache_entry>> route_cache_list;
    std::unordered_map<std::string,
        std::list<std::pair<std::string, route_cache_entry>>::iterator>
        route_cache_map;

    std::shared_mutex bans_mutex;
    std::set<http::ip_representation> bans;

    std::shared_mutex allowances_mutex;
    std::set<http::ip_representation> allowances;

#ifdef HAVE_WEBSOCKET
    std::map<std::string, ::httpserver::websocket_handler*> registered_ws_handlers;

    struct ws_upgrade_data {
        webserver_impl* impl;
        ::httpserver::websocket_handler* handler;
    };

    static void upgrade_handler(void *cls, struct MHD_Connection* connection,
                                void *req_cls, const char *extra_in,
                                size_t extra_in_size, MHD_socket sock,
                                struct MHD_UpgradeResponseHandle *urh);
#endif  // HAVE_WEBSOCKET

#if defined(HAVE_GNUTLS) && defined(MHD_OPTION_HTTPS_CERT_CALLBACK)
    mutable std::map<std::string, gnutls_certificate_credentials_t>
        sni_credentials_cache;
    mutable std::shared_mutex sni_credentials_mutex;
#endif  // HAVE_GNUTLS && MHD_OPTION_HTTPS_CERT_CALLBACK

    // Dispatch helpers (formerly methods on webserver). Each of these
    // touches both backend state on this impl and const config on the
    // owning webserver (via `parent`).
    std::shared_ptr<::httpserver::http_response> not_found_page(modded_request* mr) const;
    std::shared_ptr<::httpserver::http_response> method_not_allowed_page(modded_request* mr) const;
    std::shared_ptr<::httpserver::http_response> internal_error_page(modded_request* mr,
                                                       bool force_our = false) const;
    bool should_skip_auth(const std::string& path) const;
    void invalidate_route_cache();

    MHD_Result requests_answer_first_step(MHD_Connection* connection, modded_request* mr);
    MHD_Result requests_answer_second_step(MHD_Connection* connection,
            const char* method, const char* version, const char* upload_data,
            size_t* upload_data_size, modded_request* mr);
    MHD_Result finalize_answer(MHD_Connection* connection, modded_request* mr,
                               const char* method);
    MHD_Result complete_request(MHD_Connection* connection, modded_request* mr,
                                const char* version, const char* method);
    struct MHD_Response* get_raw_response_with_fallback(modded_request* mr);

    static struct MHD_Response* materialize_response(::httpserver::http_response* resp);
    static void decorate_mhd_response(struct MHD_Response* response,
                                      const ::httpserver::http_response& resp);

    // MHD trampolines registered with libmicrohttpd. Closure pointer is
    // `this` (webserver_impl*) for answer_to_connection, otherwise the
    // owning `webserver*` (so callbacks can read the const config bag).
    static void request_completed(void* cls, struct MHD_Connection* connection,
                                  void** con_cls, enum MHD_RequestTerminationCode toe);
    static MHD_Result answer_to_connection(void* cls, MHD_Connection* connection,
            const char* url, const char* method, const char* version,
            const char* upload_data, size_t* upload_data_size, void** con_cls);
    static MHD_Result post_iterator(void* cls, enum MHD_ValueKind kind,
            const char* key, const char* filename, const char* content_type,
            const char* transfer_encoding, const char* data, uint64_t off,
            size_t size);

    // Auxiliary MHD callbacks (formerly free functions in webserver.cpp).
    // Each takes `cls = webserver*` so it can read the const config bag.
    static MHD_Result policy_callback(void* cls, const struct sockaddr* addr,
                                      socklen_t addrlen);
    static void error_log(void* cls, const char* fmt, va_list ap);
    static void* uri_log(void* cls, const char* uri,
                         struct MHD_Connection* con);
    static void access_log(::httpserver::webserver* cls, const std::string& uri);
    static size_t unescaper_func(void* cls, struct MHD_Connection* c, char* s);

#ifdef HAVE_GNUTLS
    static int psk_cred_handler_func(void* cls, struct MHD_Connection* connection,
                                     const char* username, void** psk,
                                     size_t* psk_size);
#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
    static int sni_cert_callback_func(void* cls, struct MHD_Connection* connection,
                                      const char* server_name,
                                      gnutls_certificate_credentials_t* creds);
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#endif  // HAVE_GNUTLS
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_
