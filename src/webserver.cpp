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
#include "httpserver/websocket_handler.hpp"
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
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/create_webserver.hpp"
#include "httpserver/details/http_endpoint.hpp"
#include "httpserver/details/modded_request.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_utilities.hpp"
#include "httpserver/string_response.hpp"

struct MHD_Connection;

#define _REENTRANT 1

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#endif  // HAVE_GNUTLS

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
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

MHD_Result policy_callback(void *, const struct sockaddr*, socklen_t);
void error_log(void*, const char*, va_list);
void* uri_log(void*, const char*, struct MHD_Connection *con);
void access_log(webserver*, string);
size_t unescaper_func(void*, struct MHD_Connection*, char*);

struct compare_value {
    bool operator() (const std::pair<int, int>& left, const std::pair<int, int>& right) const {
        return left.second < right.second;
    }
};

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

// WEBSERVER
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
    bind_socket(params._bind_socket),
    max_thread_stack_size(params._max_thread_stack_size),
    use_ssl(params._use_ssl),
    use_ipv6(params._use_ipv6),
    use_dual_stack(params._use_dual_stack),
    debug(params._debug),
    pedantic(params._pedantic),
    https_mem_key(params._https_mem_key),
    https_mem_cert(params._https_mem_cert),
    https_mem_trust(params._https_mem_trust),
    https_priorities(params._https_priorities),
    cred_type(params._cred_type),
    psk_cred_handler(params._psk_cred_handler),
    digest_auth_random(params._digest_auth_random),
    nonce_nc_size(params._nonce_nc_size),
    running(false),
    default_policy(params._default_policy),
#ifdef HAVE_BAUTH
    basic_auth_enabled(params._basic_auth_enabled),
#endif  // HAVE_BAUTH
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
    not_found_resource(params._not_found_resource),
    method_not_allowed_resource(params._method_not_allowed_resource),
    internal_error_resource(params._internal_error_resource),
    file_cleanup_callback(params._file_cleanup_callback),
    auth_handler(params._auth_handler),
    auth_skip_paths(params._auth_skip_paths),
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
    client_discipline_level(params._client_discipline_level) {
        ignore_sigpipe();
        pthread_mutex_init(&mutexwait, nullptr);
        pthread_cond_init(&mutexcond, nullptr);
}

webserver::~webserver() {
    stop();
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

void webserver::sweet_kill() {
    stop();
}

void webserver::request_completed(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
    // These parameters are passed to respect the MHD interface, but are not needed here.
    std::ignore = cls;
    std::ignore = connection;
    std::ignore = toe;

    delete static_cast<details::modded_request*>(*con_cls);
}

bool webserver::register_resource(const std::string& resource, http_resource* hrm, bool family) {
    if (hrm == nullptr) {
        throw std::invalid_argument("The http_resource pointer cannot be null");
    }

    if (single_resource && ((resource != "" && resource != "/") || !family)) {
        throw std::invalid_argument("The resource should be '' or '/' and be marked as family when using a single_resource server");
    }

    details::http_endpoint idx(resource, family, true, regex_checking);

    std::unique_lock registered_resources_lock(registered_resources_mutex);
    pair<map<details::http_endpoint, http_resource*>::iterator, bool> result = registered_resources.insert(map<details::http_endpoint, http_resource*>::value_type(idx, hrm));

    if (result.second) {
        bool is_exact = !family && idx.get_url_pars().empty();
        if (is_exact) {
            registered_resources_str.insert(pair<string, http_resource*>(idx.get_url_complete(), result.first->second));
        }
        if (idx.is_regex_compiled()) {
            registered_resources_regex.insert(map<details::http_endpoint, http_resource*>::value_type(idx, hrm));
        }
        registered_resources_lock.unlock();
        invalidate_route_cache();
        return true;
    }

    return false;
}

#ifdef HAVE_WEBSOCKET
bool webserver::register_ws_resource(const std::string& resource, websocket_handler* handler) {
    if (handler == nullptr) {
        throw std::invalid_argument("The websocket_handler pointer cannot be null");
    }
    std::unique_lock lock(registered_resources_mutex);
    registered_ws_handlers[http_utils::standardize_url(resource)] = handler;
    return true;
}
#endif  // HAVE_WEBSOCKET

bool webserver::start(bool blocking) {
    struct {
        MHD_OptionItem operator ()(enum MHD_OPTION opt, intptr_t val, void *ptr = nullptr) {
            MHD_OptionItem x = {opt, val, ptr};
            return x;
        }
    } gen;
    vector<struct MHD_OptionItem> iov;

    iov.push_back(gen(MHD_OPTION_NOTIFY_COMPLETED, (intptr_t) &request_completed, nullptr));
    iov.push_back(gen(MHD_OPTION_URI_LOG_CALLBACK, (intptr_t) &uri_log, this));
    iov.push_back(gen(MHD_OPTION_EXTERNAL_LOGGER, (intptr_t) &error_log, this));
    iov.push_back(gen(MHD_OPTION_UNESCAPE_CALLBACK, (intptr_t) &unescaper_func, this));
    iov.push_back(gen(MHD_OPTION_CONNECTION_TIMEOUT, connection_timeout));
    if (bind_socket != 0) {
        iov.push_back(gen(MHD_OPTION_LISTEN_SOCKET, bind_socket));
    }

    if (start_method == http_utils::THREAD_PER_CONNECTION && (max_threads != 0 || max_thread_stack_size != 0)) {
        throw std::invalid_argument("Cannot specify maximum number of threads when using a thread per connection");
    }

    if (max_threads != 0) {
        iov.push_back(gen(MHD_OPTION_THREAD_POOL_SIZE, max_threads));
    }

    if (max_connections != 0) {
        iov.push_back(gen(MHD_OPTION_CONNECTION_LIMIT, max_connections));
    }

    if (memory_limit != 0) {
        iov.push_back(gen(MHD_OPTION_CONNECTION_MEMORY_LIMIT, memory_limit));
    }

    if (per_IP_connection_limit != 0) {
        iov.push_back(gen(MHD_OPTION_PER_IP_CONNECTION_LIMIT, per_IP_connection_limit));
    }

    if (max_thread_stack_size != 0) {
        iov.push_back(gen(MHD_OPTION_THREAD_STACK_SIZE, max_thread_stack_size));
    }

#ifdef HAVE_DAUTH
    if (nonce_nc_size != 0) {
        iov.push_back(gen(MHD_OPTION_NONCE_NC_SIZE, nonce_nc_size));
    }
#endif  // HAVE_DAUTH

    if (use_ssl) {
        // Need for const_cast to respect MHD interface that needs a void*
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_KEY, 0, reinterpret_cast<void*>(const_cast<char*>(https_mem_key.c_str()))));
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_CERT, 0, reinterpret_cast<void*>(const_cast<char*>(https_mem_cert.c_str()))));

        if (!https_mem_trust.empty()) {
            iov.push_back(gen(MHD_OPTION_HTTPS_MEM_TRUST, 0, reinterpret_cast<void*>(const_cast<char*>(https_mem_trust.c_str()))));
        }

        if (!https_priorities.empty()) {
            iov.push_back(gen(MHD_OPTION_HTTPS_PRIORITIES, 0, reinterpret_cast<void*>(const_cast<char*>(https_priorities.c_str()))));
        }
    }

#ifdef HAVE_DAUTH
    if (digest_auth_random != "") {
        // Need for const_cast to respect MHD interface that needs a char*
        iov.push_back(gen(MHD_OPTION_DIGEST_AUTH_RANDOM, digest_auth_random.size(), const_cast<char*>(digest_auth_random.c_str())));
    }
#endif  // HAVE_DAUTH

#ifdef HAVE_GNUTLS
    if (cred_type != http_utils::NONE) {
        iov.push_back(gen(MHD_OPTION_HTTPS_CRED_TYPE, cred_type));
    }

    if (psk_cred_handler != nullptr && use_ssl) {
        iov.push_back(gen(MHD_OPTION_GNUTLS_PSK_CRED_HANDLER,
                          (intptr_t)&psk_cred_handler_func, this));
    }

#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
    if (sni_callback != nullptr && use_ssl) {
        iov.push_back(gen(MHD_OPTION_HTTPS_CERT_CALLBACK,
                          (intptr_t)&sni_cert_callback_func, this));
    }
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#endif  // HAVE_GNUTLS

    if (listen_backlog > 0) {
        iov.push_back(gen(MHD_OPTION_LISTEN_BACKLOG_SIZE, listen_backlog));
    }

    if (address_reuse != 0) {
        iov.push_back(gen(MHD_OPTION_LISTENING_ADDRESS_REUSE, address_reuse));
    }

    if (connection_memory_increment > 0) {
        iov.push_back(gen(MHD_OPTION_CONNECTION_MEMORY_INCREMENT, connection_memory_increment));
    }

    if (tcp_fastopen_queue_size > 0) {
        iov.push_back(gen(MHD_OPTION_TCP_FASTOPEN_QUEUE_SIZE, tcp_fastopen_queue_size));
    }

    if (sigpipe_handled_by_app) {
        iov.push_back(gen(MHD_OPTION_SIGPIPE_HANDLED_BY_APP, 1));
    }

    if (!https_mem_dhparams.empty()) {
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_DHPARAMS, 0, const_cast<char*>(https_mem_dhparams.c_str())));
    }

    if (!https_key_password.empty()) {
        iov.push_back(gen(MHD_OPTION_HTTPS_KEY_PASSWORD, 0, const_cast<char*>(https_key_password.c_str())));
    }

    if (!https_priorities_append.empty()) {
        iov.push_back(gen(MHD_OPTION_HTTPS_PRIORITIES_APPEND, 0, const_cast<char*>(https_priorities_append.c_str())));
    }

    if (no_alpn) {
        iov.push_back(gen(MHD_OPTION_TLS_NO_ALPN, 1));
    }

    if (client_discipline_level >= 0) {
        iov.push_back(gen(MHD_OPTION_CLIENT_DISCIPLINE_LVL, client_discipline_level));
    }

    iov.push_back(gen(MHD_OPTION_END, 0, nullptr));

    int start_conf = start_method;

    if (use_ssl) {
        start_conf |= MHD_USE_SSL;
    }

    if (use_ipv6) {
        start_conf |= MHD_USE_IPv6;
    }

    if (use_dual_stack) {
        start_conf |= MHD_USE_DUAL_STACK;
    }

    if (debug) {
        start_conf |= MHD_USE_DEBUG;
    }
    if (pedantic) {
        start_conf |= MHD_USE_PEDANTIC_CHECKS;
    }

    if (deferred_enabled) {
        start_conf |= MHD_USE_SUSPEND_RESUME;
    }

#ifdef USE_FASTOPEN
    start_conf |= MHD_USE_TCP_FASTOPEN;
#endif

    if (no_listen_socket) {
        start_conf |= MHD_USE_NO_LISTEN_SOCKET;
    }

    if (no_thread_safety) {
        start_conf |= MHD_USE_NO_THREAD_SAFETY;
    }

    if (turbo) {
        start_conf |= MHD_USE_TURBO;
    }

    if (suppress_date_header) {
        start_conf |= MHD_USE_SUPPRESS_DATE_NO_CLOCK;
    }

#ifdef HAVE_WEBSOCKET
    if (!registered_ws_handlers.empty()) {
        start_conf |= MHD_ALLOW_UPGRADE;
    }
#endif  // HAVE_WEBSOCKET


    daemon = nullptr;
    if (bind_address == nullptr) {
        daemon = MHD_start_daemon(start_conf, port, &policy_callback, this,
                &answer_to_connection, this, MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_END);
    } else {
        daemon = MHD_start_daemon(start_conf, 1, &policy_callback, this,
                &answer_to_connection, this, MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_SOCK_ADDR, bind_address, MHD_OPTION_END);
    }

    if (daemon == nullptr) {
        throw std::invalid_argument("Unable to connect daemon to port: " + std::to_string(port));
    }

    bool value_onclose = false;

    running = true;

    if (blocking) {
        pthread_mutex_lock(&mutexwait);
        while (blocking && running) {
            pthread_cond_wait(&mutexcond, &mutexwait);
        }
        pthread_mutex_unlock(&mutexwait);
        value_onclose = true;
    }
    return value_onclose;
}

bool webserver::is_running() {
    return running;
}

bool webserver::stop() {
    if (!running) return false;

    pthread_mutex_lock(&mutexwait);
    running = false;
    pthread_cond_signal(&mutexcond);
    pthread_mutex_unlock(&mutexwait);

    MHD_stop_daemon(daemon);

    shutdown(bind_socket, 2);

    return true;
}

int webserver::quiesce() {
    if (daemon == nullptr) return -1;
    MHD_socket fd = MHD_quiesce_daemon(daemon);
    return static_cast<int>(fd);
}

int webserver::get_listen_fd() const {
    if (daemon == nullptr) return -1;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(daemon, MHD_DAEMON_INFO_LISTEN_FD);
    if (info == nullptr) return -1;
    return static_cast<int>(info->listen_fd);
}

unsigned int webserver::get_active_connections() const {
    if (daemon == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(daemon, MHD_DAEMON_INFO_CURRENT_CONNECTIONS);
    if (info == nullptr) return 0;
    return info->num_connections;
}

uint16_t webserver::get_bound_port() const {
    if (daemon == nullptr) return 0;
    const union MHD_DaemonInfo* info = MHD_get_daemon_info(daemon, MHD_DAEMON_INFO_BIND_PORT);
    if (info == nullptr) return 0;
    return info->port;
}

bool webserver::run() {
    if (daemon == nullptr) return false;
    return MHD_run(daemon) == MHD_YES;
}

bool webserver::run_wait(int32_t millisec) {
    if (daemon == nullptr) return false;
    return MHD_run_wait(daemon, millisec) == MHD_YES;
}

bool webserver::get_fdset(fd_set* read_fd_set, fd_set* write_fd_set, fd_set* except_fd_set, int* max_fd) {
    if (daemon == nullptr) return false;
    MHD_socket mhd_max_fd = 0;
    if (MHD_get_fdset(daemon, read_fd_set, write_fd_set, except_fd_set, &mhd_max_fd) != MHD_YES) {
        return false;
    }
    *max_fd = static_cast<int>(mhd_max_fd);
    return true;
}

bool webserver::get_timeout(uint64_t* timeout) {
    if (daemon == nullptr) return false;
    MHD_UNSIGNED_LONG_LONG mhd_timeout = 0;
    if (MHD_get_timeout(daemon, &mhd_timeout) != MHD_YES) {
        return false;
    }
    *timeout = static_cast<uint64_t>(mhd_timeout);
    return true;
}

bool webserver::add_connection(int client_socket, const struct sockaddr* addr, socklen_t addrlen) {
    if (daemon == nullptr) return false;
    return MHD_add_connection(daemon, client_socket, addr, addrlen) == MHD_YES;
}

void webserver::invalidate_route_cache() {
    std::lock_guard<std::mutex> lock(route_cache_mutex);
    route_cache_list.clear();
    route_cache_map.clear();
}

void webserver::unregister_resource(const string& resource) {
    // family does not matter - it just checks the url_normalized anyhow
    details::http_endpoint he(resource, false, true, regex_checking);
    std::unique_lock registered_resources_lock(registered_resources_mutex);

    // Invalidate cache while holding registered_resources_mutex to prevent
    // any thread from retrieving dangling resource pointers from the cache
    // after we erase from the resource maps.
    {
        std::lock_guard<std::mutex> cache_lock(route_cache_mutex);
        route_cache_list.clear();
        route_cache_map.clear();
    }

    registered_resources.erase(he);
    registered_resources.erase(he.get_url_complete());
    registered_resources_str.erase(he.get_url_complete());
    registered_resources_regex.erase(he);
}

void webserver::ban_ip(const string& ip) {
    std::unique_lock bans_lock(bans_mutex);
    ip_representation t_ip(ip);
    set<ip_representation>::iterator it = bans.find(t_ip);
    if (it != bans.end() && (t_ip.weight() < (*it).weight())) {
        bans.erase(it);
        bans.insert(t_ip);
    } else {
        bans.insert(t_ip);
    }
}

void webserver::allow_ip(const string& ip) {
    std::unique_lock allowances_lock(allowances_mutex);
    ip_representation t_ip(ip);
    set<ip_representation>::iterator it = allowances.find(t_ip);
    if (it != allowances.end() && (t_ip.weight() < (*it).weight())) {
        allowances.erase(it);
        allowances.insert(t_ip);
    } else {
        allowances.insert(t_ip);
    }
}

void webserver::unban_ip(const string& ip) {
    std::unique_lock bans_lock(bans_mutex);
    bans.erase(ip_representation(ip));
}

void webserver::disallow_ip(const string& ip) {
    std::unique_lock allowances_lock(allowances_mutex);
    allowances.erase(ip_representation(ip));
}

#ifdef HAVE_GNUTLS
// MHD_PskServerCredentialsCallback signature:
// The 'cls' parameter is our webserver pointer (passed via MHD_OPTION)
// Returns 0 on success, -1 on error
// The psk output should be allocated with malloc() - MHD will free it
int webserver::psk_cred_handler_func(void* cls,
                                      struct MHD_Connection* connection,
                                      const char* username,
                                      void** psk,
                                      size_t* psk_size) {
    std::ignore = connection;  // Not needed - we get context from cls

    webserver* ws = static_cast<webserver*>(cls);

    // Initialize output to safe values
    *psk = nullptr;
    *psk_size = 0;

    if (ws == nullptr || ws->psk_cred_handler == nullptr) {
        return -1;
    }

    std::string psk_hex = ws->psk_cred_handler(std::string(username));
    if (psk_hex.empty()) {
        return -1;
    }

    // Validate hex string before allocating memory
    size_t psk_len = psk_hex.size() / 2;
    if (psk_len == 0 || (psk_hex.size() % 2 != 0) ||
        !string_utilities::is_valid_hex(psk_hex)) {
        return -1;
    }

    // Allocate with malloc - MHD will free this
    unsigned char* psk_data = static_cast<unsigned char*>(malloc(psk_len));
    if (psk_data == nullptr) {
        return -1;
    }

    // Convert hex string to binary
    for (size_t i = 0; i < psk_len; i++) {
        psk_data[i] = static_cast<unsigned char>(
            (string_utilities::hex_char_to_val(psk_hex[i * 2]) << 4) |
             string_utilities::hex_char_to_val(psk_hex[i * 2 + 1]));
    }

    *psk = psk_data;
    *psk_size = psk_len;
    return 0;
}

#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
// SNI callback for selecting certificates based on server name
// Returns 0 on success, -1 on failure
int webserver::sni_cert_callback_func(void* cls,
                                       struct MHD_Connection* connection,
                                       const char* server_name,
                                       gnutls_certificate_credentials_t* creds) {
    std::ignore = connection;

    webserver* ws = static_cast<webserver*>(cls);
    if (ws == nullptr || ws->sni_callback == nullptr || server_name == nullptr) {
        return -1;
    }

    std::string name(server_name);

    // Check if we have cached credentials for this server name
    {
        std::shared_lock lock(ws->sni_credentials_mutex);
        auto it = ws->sni_credentials_cache.find(name);
        if (it != ws->sni_credentials_cache.end()) {
            *creds = it->second;
            return 0;
        }
    }

    // Call user's callback to get cert/key pair
    auto [cert_pem, key_pem] = ws->sni_callback(name);
    if (cert_pem.empty() || key_pem.empty()) {
        return -1;  // Use default certificate
    }

    // Create new credentials for this server name
    gnutls_certificate_credentials_t new_creds;
    if (gnutls_certificate_allocate_credentials(&new_creds) != GNUTLS_E_SUCCESS) {
        return -1;
    }

    gnutls_datum_t cert_data = {
        reinterpret_cast<unsigned char*>(const_cast<char*>(cert_pem.data())),
        static_cast<unsigned int>(cert_pem.size())
    };
    gnutls_datum_t key_data = {
        reinterpret_cast<unsigned char*>(const_cast<char*>(key_pem.data())),
        static_cast<unsigned int>(key_pem.size())
    };

    int ret = gnutls_certificate_set_x509_key_mem(new_creds, &cert_data, &key_data, GNUTLS_X509_FMT_PEM);
    if (ret != GNUTLS_E_SUCCESS) {
        gnutls_certificate_free_credentials(new_creds);
        return -1;
    }

    // Cache the credentials with double-check to avoid race condition
    {
        std::unique_lock lock(ws->sni_credentials_mutex);
        // Re-check after acquiring exclusive lock - another thread may have inserted
        auto it = ws->sni_credentials_cache.find(name);
        if (it != ws->sni_credentials_cache.end()) {
            // Another thread already cached credentials, use theirs and free ours
            gnutls_certificate_free_credentials(new_creds);
            *creds = it->second;
            return 0;
        }
        ws->sni_credentials_cache[name] = new_creds;
    }

    *creds = new_creds;
    return 0;
}
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#endif  // HAVE_GNUTLS

MHD_Result policy_callback(void *cls, const struct sockaddr* addr, socklen_t addrlen) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = addrlen;

    const auto ws = static_cast<webserver*>(cls);

    if (!ws->ban_system_enabled) return MHD_YES;

    std::shared_lock bans_lock(ws->bans_mutex);
    std::shared_lock allowances_lock(ws->allowances_mutex);
    const bool is_banned = ws->bans.count(ip_representation(addr));
    const bool is_allowed = ws->allowances.count(ip_representation(addr));

    if ((ws->default_policy == http_utils::ACCEPT && is_banned && !is_allowed) ||
        (ws->default_policy == http_utils::REJECT && (!is_allowed || is_banned))) {
        return MHD_NO;
    }

    return MHD_YES;
}

void* uri_log(void* cls, const char* uri, struct MHD_Connection *con) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = con;

    auto mr = std::make_unique<details::modded_request>();
    mr->complete_uri = uri;
    return reinterpret_cast<void*>(mr.release());
}

void error_log(void* cls, const char* fmt, va_list ap) {
    webserver* dws = static_cast<webserver*>(cls);

    std::string msg;
    msg.resize(80);  // Asssume one line will be enought most of the time.

    va_list va;
    va_copy(va, ap);  // Stash a copy in case we need to try again.

    size_t r = vsnprintf(&*msg.begin(), msg.size(), fmt, ap);
    va_end(ap);

    if (msg.size() < r) {
      msg.resize(r);
      r = vsnprintf(&*msg.begin(), msg.size(), fmt, va);
    }
    va_end(va);
    msg.resize(r);

    if (dws->log_error != nullptr) dws->log_error(msg);
}

void access_log(webserver* dws, string uri) {
    if (dws->log_access != nullptr) dws->log_access(uri);
}

size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = c;

    // THIS IS USED TO AVOID AN UNESCAPING OF URL BEFORE THE ANSWER.
    // IT IS DUE TO A BOGUS ON libmicrohttpd (V0.99) THAT PRODUCING A
    // STRING CONTAINING '\0' AFTER AN UNESCAPING, IS UNABLE TO PARSE
    // ARGS WITH get_connection_values FUNC OR lookup FUNC.
    if (s == nullptr) return 0;
    return std::char_traits<char>::length(s);
}

MHD_Result webserver::post_iterator(void *cls, enum MHD_ValueKind kind,
        const char *key, const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off, size_t size) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = kind;

    struct details::modded_request* mr = (struct details::modded_request*) cls;

    if (!filename) {
        // There is no actual file, just set the arg key/value and return.
        if (off > 0) {
            mr->dhr->grow_last_arg(key, std::string(data, size));
            return MHD_YES;
        }

        mr->dhr->set_arg(key, std::string(data, size));
        return MHD_YES;
    }

    try {
        if (mr->ws->file_upload_target != FILE_UPLOAD_DISK_ONLY) {
            mr->dhr->set_arg_flat(key, std::string(mr->dhr->get_arg(key)) + std::string(data, size));
        }

        if (*filename != '\0' && mr->ws->file_upload_target != FILE_UPLOAD_MEMORY_ONLY) {
            // either get the existing file info struct or create a new one in the file map
            http::file_info& file = mr->dhr->get_or_create_file_info(key, filename);
            // if the file_system_file_name is not filled yet, this is a new entry and the name has to be set
            // (either random or copy of the original filename)
            if (file.get_file_system_file_name().empty()) {
                if (mr->ws->generate_random_filename_on_upload) {
                    file.set_file_system_file_name(http_utils::generate_random_upload_filename(mr->ws->file_upload_dir));
                } else {
                    std::string safe_name = http_utils::sanitize_upload_filename(filename);
                    if (safe_name.empty()) {
                        return MHD_NO;
                    }
                    file.set_file_system_file_name(mr->ws->file_upload_dir + "/" + safe_name);
                }
                // to not append to an already existing file, delete an already existing file
                unlink(file.get_file_system_file_name().c_str());
                if (content_type != nullptr) {
                    file.set_content_type(content_type);
                }
                if (transfer_encoding != nullptr) {
                    file.set_transfer_encoding(transfer_encoding);
                }
            }

            // if multiple files are uploaded, a different filename or a different key indicates
            // the start of a new file, so close the previous one
            if (mr->upload_filename.empty() ||
                mr->upload_key.empty() ||
                0 != strcmp(filename, mr->upload_filename.c_str()) ||
                0 != strcmp(key, mr->upload_key.c_str())) {
                if (mr->upload_ostrm != nullptr) {
                    mr->upload_ostrm->close();
                }
            }

            if (mr->upload_ostrm == nullptr || !mr->upload_ostrm->is_open()) {
                mr->upload_key = key;
                mr->upload_filename = filename;
                mr->upload_ostrm = std::make_unique<std::ofstream>();
                mr->upload_ostrm->open(file.get_file_system_file_name(), std::ios::binary | std::ios::app);
            }

            if (size > 0) {
                mr->upload_ostrm->write(data, size);
                if (!mr->upload_ostrm->good()) {
                    return MHD_NO;
                }
            }

            // update the file size in the map
            file.grow_file_size(size);
        }
        return MHD_YES;
    } catch(const http::generateFilenameException& e) {
        return MHD_NO;
    }
}

#ifdef HAVE_WEBSOCKET
static void decode_websocket_buffer(struct MHD_WebSocketStream* ws_stream,
                                    websocket_handler* handler,
                                    websocket_session& session,
                                    const char* buf, size_t buf_len) {
    size_t offset = 0;
    while (offset < buf_len && session.is_valid()) {
        char* frame_data = nullptr;
        size_t frame_len = 0;
        size_t step = 0;
        int status = MHD_websocket_decode(ws_stream,
                                           buf + offset,
                                           buf_len - offset,
                                           &step,
                                           &frame_data,
                                           &frame_len);
        offset += step;
        switch (status) {
            case MHD_WEBSOCKET_STATUS_TEXT_FRAME:
                handler->on_message(session, std::string_view(frame_data, frame_len));
                MHD_websocket_free(ws_stream, frame_data);
                break;
            case MHD_WEBSOCKET_STATUS_BINARY_FRAME:
                handler->on_binary(session, frame_data, frame_len);
                MHD_websocket_free(ws_stream, frame_data);
                break;
            case MHD_WEBSOCKET_STATUS_PING_FRAME:
                handler->on_ping(session, std::string_view(frame_data, frame_len));
                MHD_websocket_free(ws_stream, frame_data);
                break;
            case MHD_WEBSOCKET_STATUS_CLOSE_FRAME: {
                uint16_t close_code = 1000;
                std::string close_reason;
                if (frame_len >= 2) {
                    close_code = static_cast<uint16_t>(
                        (static_cast<unsigned char>(frame_data[0]) << 8) |
                         static_cast<unsigned char>(frame_data[1]));
                    if (frame_len > 2) {
                        close_reason.assign(frame_data + 2, frame_len - 2);
                    }
                }
                handler->on_close(session, close_code, close_reason);
                MHD_websocket_free(ws_stream, frame_data);
                // Send close response and end the loop
                session.close(close_code, close_reason);
                break;
            }
            case MHD_WEBSOCKET_STATUS_OK:
                // Need more data - go back to recv
                if (frame_data != nullptr) {
                    MHD_websocket_free(ws_stream, frame_data);
                }
                break;
            default:
                // Protocol error or unknown frame
                if (frame_data != nullptr) {
                    MHD_websocket_free(ws_stream, frame_data);
                }
                session.close(1002, "Protocol error");
                break;
        }
        // If decode consumed no bytes, we need more data
        if (step == 0) break;
    }
}

void webserver::upgrade_handler(void *cls, struct MHD_Connection* connection,
                                void *req_cls, const char *extra_in,
                                size_t extra_in_size, MHD_socket sock,
                                struct MHD_UpgradeResponseHandle *urh) {
    std::ignore = connection;
    std::ignore = req_cls;

    ws_upgrade_data* data = static_cast<ws_upgrade_data*>(cls);
    websocket_handler* handler = data->handler;
    delete data;

    // Create a WebSocket stream for this connection
    struct MHD_WebSocketStream* ws_stream = nullptr;
    int ws_result = MHD_websocket_stream_init(&ws_stream,
                                               MHD_WEBSOCKET_FLAG_SERVER | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS,
                                               0);
    if (ws_result != MHD_WEBSOCKET_STATUS_OK || ws_stream == nullptr) {
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        return;
    }

    websocket_session session(sock, urh, ws_stream);
    handler->on_open(session);

    // Process any initial data that MHD may have buffered
    if (extra_in != nullptr && extra_in_size > 0) {
        decode_websocket_buffer(ws_stream, handler, session, extra_in, extra_in_size);
    }

    // Receive loop
    char buf[4096];
    while (session.is_valid()) {
        ssize_t got = recv(sock, buf, sizeof(buf), 0);
        if (got <= 0) break;

        decode_websocket_buffer(ws_stream, handler, session,
                                buf, static_cast<size_t>(got));
    }

    // Session destructor will free ws_stream and close urh
}
#endif  // HAVE_WEBSOCKET

std::shared_ptr<http_response> webserver::not_found_page(details::modded_request* mr) const {
    if (not_found_resource != nullptr) {
        return not_found_resource(*mr->dhr);
    } else {
        return std::make_shared<string_response>(NOT_FOUND_ERROR, http_utils::http_not_found);
    }
}

std::shared_ptr<http_response> webserver::method_not_allowed_page(details::modded_request* mr) const {
    if (method_not_allowed_resource != nullptr) {
        return method_not_allowed_resource(*mr->dhr);
    } else {
        return std::make_shared<string_response>(METHOD_ERROR, http_utils::http_method_not_allowed);
    }
}

std::shared_ptr<http_response> webserver::internal_error_page(details::modded_request* mr, bool force_our) const {
    if (internal_error_resource != nullptr && !force_our) {
        return internal_error_resource(*mr->dhr);
    } else {
        return std::make_shared<string_response>(GENERIC_ERROR, http_utils::http_internal_server_error);
    }
}

static std::string normalize_path(const std::string& path) {
    std::vector<std::string> segments;
    std::string::size_type start = 0;
    // Skip leading slash
    if (!path.empty() && path[0] == '/') {
        start = 1;
    }
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string seg = path.substr(start, end - start);
        if (seg == "..") {
            if (!segments.empty()) segments.pop_back();
        } else if (!seg.empty() && seg != ".") {
            segments.push_back(seg);
        }
        start = end + 1;
    }
    std::string normalized = "/";
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) normalized += "/";
        normalized += segments[i];
    }
    return normalized;
}

bool webserver::should_skip_auth(const std::string& path) const {
    std::string normalized = normalize_path(path);

    for (const auto& skip_path : auth_skip_paths) {
        if (skip_path == normalized) return true;
        // Support wildcard suffix (e.g., "/public/*")
        if (skip_path.size() > 2 && skip_path.back() == '*' &&
            skip_path[skip_path.size() - 2] == '/') {
            std::string prefix = skip_path.substr(0, skip_path.size() - 1);
            if (normalized.compare(0, prefix.size(), prefix) == 0) return true;
        }
    }
    return false;
}

MHD_Result webserver::requests_answer_first_step(MHD_Connection* connection, struct details::modded_request* mr) {
    mr->dhr.reset(new http_request(connection, unescaper));
    mr->dhr->set_file_cleanup_callback(file_cleanup_callback);

    if (!mr->has_body) {
        return MHD_YES;
    }

    mr->dhr->set_content_size_limit(content_size_limit);
    const char *encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, http_utils::http_header_content_type);

    if (post_process_enabled &&
        (nullptr != encoding &&
            ((0 == strncasecmp(http_utils::http_post_encoding_form_urlencoded, encoding, strlen(http_utils::http_post_encoding_form_urlencoded))) ||
             (0 == strncasecmp(http_utils::http_post_encoding_multipart_formdata, encoding, strlen(http_utils::http_post_encoding_multipart_formdata)))))) {
        const size_t post_memory_limit(32 * 1024);  // Same as #MHD_POOL_SIZE_DEFAULT
        mr->pp = MHD_create_post_processor(connection, post_memory_limit, &post_iterator, mr);
    } else {
        mr->pp = nullptr;
    }
    return MHD_YES;
}

MHD_Result webserver::requests_answer_second_step(MHD_Connection* connection, const char* method,
        const char* version, const char* upload_data,
        size_t* upload_data_size, struct details::modded_request* mr) {
    if (0 == *upload_data_size) return complete_request(connection, mr, version, method);

    if (mr->has_body) {
#ifdef DEBUG
        std::cout << "Writing content: " << std::string(upload_data, *upload_data_size) << std::endl;
#endif  // DEBUG
        // The post iterator is only created from the libmicrohttpd for content of type
        // multipart/form-data and application/x-www-form-urlencoded
        // all other content (which is indicated by mr-pp == nullptr)
        // has to be put to the content even if put_processed_data_to_content is set to false
        if (mr->pp == nullptr || put_processed_data_to_content) {
            mr->dhr->grow_content(upload_data, *upload_data_size);
        }

        if (mr->pp != nullptr) {
            mr->ws = this;
            MHD_post_process(mr->pp, upload_data, *upload_data_size);
            if (mr->upload_ostrm != nullptr && mr->upload_ostrm->is_open()) {
                mr->upload_ostrm->close();
            }
        }
    }

    *upload_data_size = 0;
    return MHD_YES;
}

struct MHD_Response* webserver::get_raw_response_with_fallback(details::modded_request* mr) {
    try {
        struct MHD_Response* raw = mr->dhrs->get_raw_response();
        if (raw == nullptr) {
            mr->dhrs = internal_error_page(mr);
            raw = mr->dhrs->get_raw_response();
        }
        return raw;
    } catch(const std::invalid_argument&) {
        try {
            mr->dhrs = not_found_page(mr);
            return mr->dhrs->get_raw_response();
        } catch(...) {
            return nullptr;
        }
    } catch(...) {
        try {
            mr->dhrs = internal_error_page(mr);
            return mr->dhrs->get_raw_response();
        } catch(...) {
            return nullptr;
        }
    }
}

MHD_Result webserver::finalize_answer(MHD_Connection* connection, struct details::modded_request* mr, const char* method) {
    int to_ret = MHD_NO;

#ifdef HAVE_WEBSOCKET
    // Check for WebSocket upgrade request before normal resource dispatch
    {
        const char* upgrade_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_UPGRADE);
        if (upgrade_header != nullptr && 0 == strcasecmp(upgrade_header, "websocket")) {
            // RFC 6455 handshake validation
            const char* connection_header = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONNECTION);
            const char* ws_version = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Sec-WebSocket-Version");
            const char* ws_key = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Sec-WebSocket-Key");

            // Validate required headers per RFC 6455 Section 4.2.1
            auto send_bad_request = [&]() -> MHD_Result {
                struct MHD_Response* bad_response = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
                MHD_Result ret = (MHD_Result) MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, bad_response);
                MHD_destroy_response(bad_response);
                return ret;
            };

            if (connection_header == nullptr || strcasestr(connection_header, "Upgrade") == nullptr) {
                return send_bad_request();
            }
            if (ws_version == nullptr || strcmp(ws_version, "13") != 0) {
                return send_bad_request();
            }
            if (ws_key == nullptr || ws_key[0] == '\0') {
                return send_bad_request();
            }

            std::shared_lock lock(registered_resources_mutex);
            auto ws_it = registered_ws_handlers.find(mr->standardized_url);
            if (ws_it != registered_ws_handlers.end()) {
                websocket_handler* handler = ws_it->second;
                lock.unlock();

                ws_upgrade_data* data = new ws_upgrade_data{this, handler};
                struct MHD_Response* response = MHD_create_response_for_upgrade(&upgrade_handler, data);
                if (response != nullptr) {
                    // Add required WebSocket response headers
                    MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, "websocket");

                    // Compute Sec-WebSocket-Accept from client's key (RFC 6455 Section 4.2.2)
                    char accept_header[29];  // Base64 of SHA-1 = 28 chars + null
                    if (MHD_websocket_create_accept_header(ws_key, accept_header) == MHD_WEBSOCKET_STATUS_OK) {
                        MHD_add_response_header(response, "Sec-WebSocket-Accept", accept_header);
                    }

                    to_ret = MHD_queue_response(connection, MHD_HTTP_SWITCHING_PROTOCOLS, response);
                    MHD_destroy_response(response);
                    return (MHD_Result) to_ret;
                }
                delete data;
            }
        }
    }
#endif  // HAVE_WEBSOCKET

    map<string, http_resource*>::iterator fe;

    http_resource* hrm;

    bool found = false;
    struct MHD_Response* raw_response;
    {
        std::shared_lock registered_resources_lock(registered_resources_mutex);
        if (!single_resource) {
            const char* st_url = mr->standardized_url.c_str();
            fe = registered_resources_str.find(st_url);
            if (fe == registered_resources_str.end()) {
                if (regex_checking) {
                    details::http_endpoint endpoint(st_url, false, false, false);

                    // Data needed for parameter extraction after match.
                    // On cache hit, we copy these while holding the cache lock
                    // to avoid use-after-free if another thread invalidates cache.
                    vector<string> matched_url_pars;
                    vector<int> matched_chunks;

                    // Check the LRU route cache first
                    {
                        std::lock_guard<std::mutex> cache_lock(route_cache_mutex);
                        auto cache_it = route_cache_map.find(mr->standardized_url);
                        if (cache_it != route_cache_map.end()) {
                            // Cache hit — move to front of LRU list
                            route_cache_list.splice(route_cache_list.begin(), route_cache_list, cache_it->second);
                            const route_cache_entry& cached = cache_it->second->second;
                            matched_url_pars = cached.matched_endpoint.get_url_pars();
                            matched_chunks = cached.matched_endpoint.get_chunk_positions();
                            hrm = cached.resource;
                            found = true;
                        }
                    }

                    if (!found) {
                        // Cache miss — perform regex scan
                        map<details::http_endpoint, http_resource*>::iterator found_endpoint;

                        size_t len = 0;
                        size_t tot_len = 0;
                        for (auto it = registered_resources_regex.begin(); it != registered_resources_regex.end(); ++it) {
                            size_t endpoint_pieces_len = it->first.get_url_pieces().size();
                            size_t endpoint_tot_len = it->first.get_url_complete().size();
                            if (!found || endpoint_pieces_len > len || (endpoint_pieces_len == len && endpoint_tot_len > tot_len)) {
                                if (it->first.match(endpoint)) {
                                    found = true;
                                    len = endpoint_pieces_len;
                                    tot_len = endpoint_tot_len;
                                    found_endpoint = it;
                                }
                            }
                        }

                        if (found) {
                            // Safe to reference: registered_resources_mutex (shared) is still held
                            matched_url_pars = found_endpoint->first.get_url_pars();
                            matched_chunks = found_endpoint->first.get_chunk_positions();
                            hrm = found_endpoint->second;

                            // Store in LRU cache
                            {
                                std::lock_guard<std::mutex> cache_lock(route_cache_mutex);
                                route_cache_list.emplace_front(mr->standardized_url, route_cache_entry{found_endpoint->first, hrm});
                                route_cache_map[mr->standardized_url] = route_cache_list.begin();

                                if (route_cache_map.size() > ROUTE_CACHE_MAX_SIZE) {
                                    route_cache_map.erase(route_cache_list.back().first);
                                    route_cache_list.pop_back();
                                }
                            }
                        }
                    }

                    // Extract URL parameters from matched endpoint
                    if (found) {
                        const auto& url_pieces = endpoint.get_url_pieces();
                        for (unsigned int i = 0; i < matched_url_pars.size(); i++) {
                            if (matched_chunks[i] >= 0 && static_cast<size_t>(matched_chunks[i]) < url_pieces.size()) {
                                mr->dhr->set_arg(matched_url_pars[i], url_pieces[matched_chunks[i]]);
                            }
                        }
                    }
                }
            } else {
                hrm = fe->second;
                found = true;
            }
        } else {
            hrm = registered_resources.begin()->second;
            found = true;
        }
    }

    // Check centralized authentication if handler is configured
    if (found && auth_handler != nullptr) {
        std::string path(mr->dhr->get_path());
        if (!should_skip_auth(path)) {
            std::shared_ptr<http_response> auth_response = auth_handler(*mr->dhr);
            if (auth_response != nullptr) {
                mr->dhrs = auth_response;
                found = false;  // Skip resource rendering, go directly to response
            }
        }
    }

    if (found) {
        try {
            if (mr->pp != nullptr) {
                MHD_destroy_post_processor(mr->pp);
                mr->pp = nullptr;
            }
            if (hrm->is_allowed(method)) {
                mr->dhrs = ((hrm)->*(mr->callback))(*mr->dhr);  // copy in memory (move in case)
                if (mr->dhrs.get() == nullptr || mr->dhrs->get_response_code() == -1) {
                    mr->dhrs = internal_error_page(mr);
                }
            } else {
                mr->dhrs = method_not_allowed_page(mr);

                vector<string> allowed_methods = hrm->get_allowed_methods();
                if (allowed_methods.size() > 0) {
                    string header_value = allowed_methods[0];
                    for (auto it = allowed_methods.cbegin() + 1; it != allowed_methods.cend(); ++it) {
                        header_value += ", " + (*it);
                    }
                    mr->dhrs->with_header(http_utils::http_header_allow, header_value);
                }
            }
        } catch(const std::exception& e) {
            mr->dhrs = internal_error_page(mr);
        } catch(...) {
            mr->dhrs = internal_error_page(mr);
        }
    } else if (mr->dhrs == nullptr) {
        mr->dhrs = not_found_page(mr);
    }

    raw_response = get_raw_response_with_fallback(mr);
    if (raw_response == nullptr) {
        mr->dhrs = internal_error_page(mr, true);
        raw_response = mr->dhrs->get_raw_response();
    }
    mr->dhrs->decorate_response(raw_response);
    to_ret = mr->dhrs->enqueue_response(connection, raw_response);
    MHD_destroy_response(raw_response);
    return (MHD_Result) to_ret;
}

MHD_Result webserver::complete_request(MHD_Connection* connection, struct details::modded_request* mr, const char* version, const char* method) {
    mr->ws = this;

    mr->dhr->set_path(mr->standardized_url);
    mr->dhr->set_method(method);
    mr->dhr->set_version(version);

    return finalize_answer(connection, mr, method);
}

MHD_Result webserver::answer_to_connection(void* cls, MHD_Connection* connection, const char* url, const char* method,
        const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls) {
    struct details::modded_request* mr = static_cast<struct details::modded_request*>(*con_cls);

    if (mr->dhr) {
        return static_cast<webserver*>(cls)->requests_answer_second_step(connection, method, version, upload_data, upload_data_size, mr);
    }

    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);

    if (conninfo != nullptr && static_cast<webserver*>(cls)->tcp_nodelay) {
        int yes = 1;
        setsockopt(conninfo->connect_fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&yes), sizeof(int));
    }

    std::string t_url = url;

    base_unescaper(&t_url, static_cast<webserver*>(cls)->unescaper);
    mr->standardized_url = http_utils::standardize_url(t_url);

    mr->has_body = false;

    access_log(static_cast<webserver*>(cls), mr->complete_uri + " METHOD: " + method);

    // Case-sensitive per RFC 7230 §3.1.1: HTTP method is case-sensitive.
    if (0 == strcmp(method, http_utils::http_method_get)) {
        mr->callback = &http_resource::render_GET;
    } else if (0 == strcmp(method, http_utils::http_method_post)) {
        mr->callback = &http_resource::render_POST;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_put)) {
        mr->callback = &http_resource::render_PUT;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_delete)) {
        mr->callback = &http_resource::render_DELETE;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_patch)) {
        mr->callback = &http_resource::render_PATCH;
        mr->has_body = true;
    } else if (0 == strcmp(method, http_utils::http_method_head)) {
        mr->callback = &http_resource::render_HEAD;
    } else if (0 == strcmp(method, http_utils::http_method_connect)) {
        mr->callback = &http_resource::render_CONNECT;
    } else if (0 == strcmp(method, http_utils::http_method_trace)) {
        mr->callback = &http_resource::render_TRACE;
    } else if (0 == strcmp(method, http_utils::http_method_options)) {
        mr->callback = &http_resource::render_OPTIONS;
    }

    return static_cast<webserver*>(cls)->requests_answer_first_step(connection, mr);
}

}  // namespace httpserver
