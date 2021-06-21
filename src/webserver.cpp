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

#include <fcntl.h>
#include <microhttpd_ws.h>
#include <iostream>

#include <errno.h>
#include <microhttpd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <algorithm>
#include <iosfwd>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "httpserver/create_webserver.hpp"
#include "httpserver/websocket.hpp"
#include "httpserver/websocket_handler.hpp"
#include "httpserver/details/http_endpoint.hpp"
#include "httpserver/details/modded_request.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/string_response.hpp"

struct MHD_Connection;

#define _REENTRANT 1

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

#define PAGE_INVALID_WEBSOCKET_REQUEST \
  "Invalid WebSocket request!"

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
void* uri_log(void*, const char*);
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
    digest_auth_random(params._digest_auth_random),
    nonce_nc_size(params._nonce_nc_size),
    running(false),
    default_policy(params._default_policy),
    basic_auth_enabled(params._basic_auth_enabled),
    digest_auth_enabled(params._digest_auth_enabled),
    regex_checking(params._regex_checking),
    ban_system_enabled(params._ban_system_enabled),
    post_process_enabled(params._post_process_enabled),
    deferred_enabled(params._deferred_enabled),
    single_resource(params._single_resource),
    tcp_nodelay(params._tcp_nodelay),
    not_found_resource(params._not_found_resource),
    method_not_allowed_resource(params._method_not_allowed_resource),
    internal_error_resource(params._internal_error_resource) {
        ignore_sigpipe();
        pthread_mutex_init(&mutexwait, NULL);
        pthread_cond_init(&mutexcond, NULL);
}

webserver::~webserver() {
    stop();
    pthread_mutex_destroy(&mutexwait);
    pthread_cond_destroy(&mutexcond);
}

void webserver::sweet_kill() {
    stop();
}

void webserver::request_completed(void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe) {
    // These parameters are passed to respect the MHD interface, but are not needed here.
    std::ignore = cls;
    std::ignore = connection;
    std::ignore = toe;

    details::modded_request* mr = static_cast<details::modded_request*>(*con_cls);
    if (mr == 0x0) return;

    delete mr;
    mr = 0x0;
}

bool webserver::register_resource(const std::string& resource, http_resource* hrm, bool family) {
    if (single_resource && ((resource != "" && resource != "/") || !family)) {
        throw std::invalid_argument("The resource should be '' or '/' and be marked as family when using a single_resource server");
    }

    details::http_endpoint idx(resource, family, true, regex_checking);

    pair<map<details::http_endpoint, http_resource*>::iterator, bool> result = registered_resources.insert(map<details::http_endpoint, http_resource*>::value_type(idx, hrm));

    if (result.second) {
        registered_resources_str.insert(pair<string, http_resource*>(idx.get_url_complete(), result.first->second));
    }

    return result.second;
}

bool webserver::start(bool blocking) {
    struct {
        MHD_OptionItem operator ()(enum MHD_OPTION opt, intptr_t val, void *ptr = 0) {
            MHD_OptionItem x = {opt, val, ptr};
            return x;
        }
    } gen;
    vector<struct MHD_OptionItem> iov;

    iov.push_back(gen(MHD_OPTION_NOTIFY_COMPLETED, (intptr_t) &request_completed, NULL));
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

    if (nonce_nc_size != 0) {
        iov.push_back(gen(MHD_OPTION_NONCE_NC_SIZE, nonce_nc_size));
    }

    if (use_ssl) {
        // Need for const_cast to respect MHD interface that needs a void*
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_KEY, 0, reinterpret_cast<void*>(const_cast<char*>(https_mem_key.c_str()))));
    }

    if (use_ssl) {
        // Need for const_cast to respect MHD interface that needs a void*
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_CERT, 0, reinterpret_cast<void*>(const_cast<char*>(https_mem_cert.c_str()))));
    }

    if (https_mem_trust != "" && use_ssl) {
        // Need for const_cast to respect MHD interface that needs a void*
        iov.push_back(gen(MHD_OPTION_HTTPS_MEM_TRUST, 0, reinterpret_cast<void*>(const_cast<char*>(https_mem_trust.c_str()))));
    }

    if (https_priorities != "" && use_ssl) {
        // Need for const_cast to respect MHD interface that needs a void*
        iov.push_back(gen(MHD_OPTION_HTTPS_PRIORITIES, 0, reinterpret_cast<void*>(const_cast<char*>(https_priorities.c_str()))));
    }

    if (digest_auth_random != "") {
        // Need for const_cast to respect MHD interface that needs a char*
        iov.push_back(gen(MHD_OPTION_DIGEST_AUTH_RANDOM, digest_auth_random.size(), const_cast<char*>(digest_auth_random.c_str())));
    }

#ifdef HAVE_GNUTLS
    if (cred_type != http_utils::NONE) {
        iov.push_back(gen(MHD_OPTION_HTTPS_CRED_TYPE, cred_type));
    }
#endif  // HAVE_GNUTLS

    iov.push_back(gen(MHD_OPTION_END, 0, NULL));

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
    start_conf |= MHD_USE_ERROR_LOG;
    start_conf |= MHD_ALLOW_UPGRADE;

#ifdef USE_FASTOPEN
    start_conf |= MHD_USE_TCP_FASTOPEN;
#endif

    daemon = NULL;
    if (bind_address == 0x0) {
        daemon = MHD_start_daemon(start_conf, port, &policy_callback, this,
                &answer_to_connection, this, MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_END);
    } else {
        daemon = MHD_start_daemon(start_conf, 1, &policy_callback, this,
                &answer_to_connection, this, MHD_OPTION_ARRAY,
                &iov[0], MHD_OPTION_SOCK_ADDR, bind_address, MHD_OPTION_END);
    }

    if (daemon == NULL) {
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

void webserver::unregister_resource(const string& resource) {
    // family does not matter - it just checks the url_normalized anyhow
    details::http_endpoint he(resource, false, true, regex_checking);
    registered_resources.erase(he);
    registered_resources.erase(he.get_url_complete());
    registered_resources_str.erase(he.get_url_complete());
}

void webserver::ban_ip(const string& ip) {
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
    bans.erase(ip_representation(ip));
}

void webserver::disallow_ip(const string& ip) {
    allowances.erase(ip_representation(ip));
}

MHD_Result policy_callback(void *cls, const struct sockaddr* addr, socklen_t addrlen) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = addrlen;

    if (!(static_cast<webserver*>(cls))->ban_system_enabled) return MHD_YES;

    if ((((static_cast<webserver*>(cls))->default_policy == http_utils::ACCEPT) &&
                ((static_cast<webserver*>(cls))->bans.count(ip_representation(addr))) &&
                (!(static_cast<webserver*>(cls))->allowances.count(ip_representation(addr)))) ||
            (((static_cast<webserver*>(cls))->default_policy == http_utils::REJECT) &&
             ((!(static_cast<webserver*>(cls))->allowances.count(ip_representation(addr))) ||
              ((static_cast<webserver*>(cls))->bans.count(ip_representation(addr)))))) {
        return MHD_NO;
    }

    return MHD_YES;
}

void* uri_log(void* cls, const char* uri) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;

    struct details::modded_request* mr = new details::modded_request();
    mr->complete_uri = new string(uri);
    mr->second = false;
    return reinterpret_cast<void*>(mr);
}

void error_log(void* cls, const char* fmt, va_list ap) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = ap;

    webserver* dws = static_cast<webserver*>(cls);
    if (dws->log_error != 0x0) dws->log_error(fmt);
}

void access_log(webserver* dws, string uri) {
    if (dws->log_access != 0x0) dws->log_access(uri);
}

size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = cls;
    std::ignore = c;

    // THIS IS USED TO AVOID AN UNESCAPING OF URL BEFORE THE ANSWER.
    // IT IS DUE TO A BOGUS ON libmicrohttpd (V0.99) THAT PRODUCING A
    // STRING CONTAINING '\0' AFTER AN UNESCAPING, IS UNABLE TO PARSE
    // ARGS WITH get_connection_values FUNC OR lookup FUNC.
    return std::string(s).size();
}

MHD_Result webserver::post_iterator(void *cls, enum MHD_ValueKind kind,
        const char *key, const char *filename, const char *content_type,
        const char *transfer_encoding, const char *data, uint64_t off, size_t size) {
    // Parameter needed to respect MHD interface, but not needed here.
    std::ignore = kind;
    std::ignore = filename;
    std::ignore = content_type;
    std::ignore = transfer_encoding;
    std::ignore = off;

    struct details::modded_request* mr = (struct details::modded_request*) cls;
    mr->dhr->set_arg(key, mr->dhr->get_arg(key) + std::string(data, size));
    return MHD_YES;
}

const std::shared_ptr<http_response> webserver::not_found_page(details::modded_request* mr) const {
    if (not_found_resource != 0x0) {
        return not_found_resource(*mr->dhr);
    } else {
        return std::shared_ptr<http_response>(new string_response(NOT_FOUND_ERROR, http_utils::http_not_found));
    }
}

const std::shared_ptr<http_response> webserver::method_not_allowed_page(details::modded_request* mr) const {
    if (method_not_allowed_resource != 0x0) {
        return method_not_allowed_resource(*mr->dhr);
    } else {
        return std::shared_ptr<http_response>(new string_response(METHOD_ERROR, http_utils::http_method_not_allowed));
    }
}

const std::shared_ptr<http_response> webserver::internal_error_page(details::modded_request* mr, bool force_our) const {
    if (internal_error_resource != 0x0 && !force_our) {
        return internal_error_resource(*mr->dhr);
    } else {
        return std::shared_ptr<http_response>(new string_response(GENERIC_ERROR, http_utils::http_internal_server_error, "text/plain"));
    }
}

MHD_Result webserver::requests_answer_first_step(MHD_Connection* connection, struct details::modded_request* mr) {
    mr->second = true;
    mr->dhr = new http_request(connection, unescaper);

    if (!mr->has_body) {
        return MHD_YES;
    }

    mr->dhr->set_content_size_limit(content_size_limit);
    const char *encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, http_utils::http_header_content_type);

    if (post_process_enabled &&
        (0x0 != encoding &&
            ((0 == strncasecmp(http_utils::http_post_encoding_form_urlencoded, encoding, strlen(http_utils::http_post_encoding_form_urlencoded))) ||
             (0 == strncasecmp(http_utils::http_post_encoding_multipart_formdata, encoding, strlen(http_utils::http_post_encoding_multipart_formdata)))))) {
        const size_t post_memory_limit(32 * 1024);  // Same as #MHD_POOL_SIZE_DEFAULT
        mr->pp = MHD_create_post_processor(connection, post_memory_limit, &post_iterator, mr);
    } else {
        mr->pp = NULL;
    }
    return MHD_YES;
}

MHD_Result webserver::requests_answer_second_step(MHD_Connection* connection, const char* method,
        const char* version, const char* upload_data,
        size_t* upload_data_size, struct details::modded_request* mr) {
    if (0 == *upload_data_size) return complete_request(connection, mr, version, method);

    if (mr->has_body) {
#ifdef DEBUG
        std::cout << "Writing content: " << upload_data << std::endl;
#endif  // DEBUG
        mr->dhr->grow_content(upload_data, *upload_data_size);

        if (mr->pp != NULL) MHD_post_process(mr->pp, upload_data, *upload_data_size);
    }

    *upload_data_size = 0;
    return MHD_YES;
}

/**
 * Change socket to blocking.
 *
 * @param fd the socket to manipulate
 */
static void
make_blocking (MHD_socket fd)
{
#if defined(MHD_POSIX_SOCKETS)
  int flags;

  flags = fcntl (fd, F_GETFL);
  if (-1 == flags)
    return;
  if ((flags & ~O_NONBLOCK) != flags)
    if (-1 == fcntl (fd, F_SETFL, flags & ~O_NONBLOCK))
      abort ();
#elif defined(MHD_WINSOCK_SOCKETS)
  unsigned long flags = 0;

  ioctlsocket (fd, FIONBIO, &flags);
#endif /* MHD_WINSOCK_SOCKETS */

}

/**
* Parses received data from the TCP/IP socket with the websocket stream
*
* @param cu           The connected user
* @param new_name     The new user name
* @param new_name_len The length of the new name
* @return             0 on success, other values on error
*/
int webserver::connecteduser_parse_received_websocket_stream (websocket* cu,
                                                              char* buf,
                                                              size_t buf_len)
{
  size_t buf_offset = 0;
  while (buf_offset < buf_len)
  {
    size_t new_offset = 0;
    char *frame_data = NULL;
    size_t frame_len  = 0;
    int status = MHD_websocket_decode (cu->ws,
                                       buf + buf_offset,
                                       buf_len - buf_offset,
                                       &new_offset,
                                       &frame_data,
                                       &frame_len);
    if (0 > status)
    {
      /* an error occurred and the connection must be closed */
      if (NULL != frame_data)
      {
        /* depending on the WebSocket flag */
        /* MHD_WEBSOCKET_FLAG_GENERATE_CLOSE_FRAMES_ON_ERROR */
        /* close frames might be generated on errors */
        cu->send_raw(frame_data, frame_len);
        MHD_websocket_free (cu->ws, frame_data);
      }
      return 1;
    }
    else
    {
      buf_offset += new_offset;

      if (0 < status)
      {
        /* the frame is complete */
        switch (status)
        {
        case MHD_WEBSOCKET_STATUS_TEXT_FRAME:
        case MHD_WEBSOCKET_STATUS_BINARY_FRAME:
          /**
           * a text or binary frame has been received.
           * in this chat server example we use a simple protocol where
           * the JavaScript added a prefix like "<command>|<to_user_id>|data".
           * Some examples:
           * "||test" means a regular chat message to everyone with the message "test".
           * "private|1|secret" means a private chat message to user with id 1 with the message "secret".
           * "name||MyNewName" means that the user requests a rename to "MyNewName"
           * "ping|1|" means that the user with id 1 shall get a ping
           *
           * Binary data is handled here like text data.
           * The difference in the data is only checked by the JavaScript.
           */
          cu->insert_into_receive_queue(std::string(frame_data, frame_len));
          MHD_websocket_free (cu->ws,
                              frame_data);
          return 0;

        case MHD_WEBSOCKET_STATUS_CLOSE_FRAME:
          /* if we receive a close frame, we will respond with one */
          MHD_websocket_free (cu->ws,
                              frame_data);
          {
            char*result = NULL;
            size_t result_len = 0;
            int er = MHD_websocket_encode_close (cu->ws,
                                                 MHD_WEBSOCKET_CLOSEREASON_REGULAR,
                                                 NULL,
                                                 0,
                                                 &result,
                                                 &result_len);
            if (MHD_WEBSOCKET_STATUS_OK == er)
            {
              cu->send_raw(result, result_len);
              MHD_websocket_free (cu->ws, result);
            }
          }
          return 1;

        case MHD_WEBSOCKET_STATUS_PING_FRAME:
          /* if we receive a ping frame, we will respond */
          /* with the corresponding pong frame */
          std::cerr << "ping not yet implemented" << std::endl;
          return 0;

        case MHD_WEBSOCKET_STATUS_PONG_FRAME:
          /* if we receive a pong frame, */
          /* we will check whether we requested this frame and */
          /* whether it is the last requested pong */
          std::cerr << "pong not yet implemented" << std::endl;
          return 0;

        default:
          /* This case should really never happen, */
          /* because there are only five types of (finished) websocket frames. */
          /* If it is ever reached, it means that there is memory corruption. */
          MHD_websocket_free (cu->ws,
                              frame_data);
          return 1;
        }
      }
    }
  }

  return 0;
}

/**
 * Receives messages from the TCP/IP socket and
 * initializes the connected user.
 *
 * @param cls The connected user
 * @return    Always NULL
 */
void* webserver::connecteduser_receive_messages (void* cls)
{
    websocket* cu = (websocket*)cls;

    /* make the socket blocking */
    make_blocking (cu->fd);

    /* initialize the web socket stream for encoding/decoding */
    int result = MHD_websocket_stream_init (&cu->ws,
                                            MHD_WEBSOCKET_FLAG_SERVER
                                            | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS,
                                            0);
    if (MHD_WEBSOCKET_STATUS_OK != result)
    {
        MHD_upgrade_action (cu->urh,
                            MHD_UPGRADE_ACTION_CLOSE);
        free (cu);
        return NULL;
    }

    /* start the message-send thread */
    std::thread thread = cu->ws_handler->handle_websocket(cu);

    /* start by parsing extra data MHD may have already read, if any */
    if (0 != cu->extra_in_size)
    {
        if (0 != connecteduser_parse_received_websocket_stream (cu,
                                                                cu->extra_in,
                                                                cu->extra_in_size))
        {
            cu->disconnect_ = true;
            thread.join();
            struct MHD_UpgradeResponseHandle* urh = cu->urh;
            if (NULL != urh)
            {
                cu->urh = NULL;
                MHD_upgrade_action (urh,
                                    MHD_UPGRADE_ACTION_CLOSE);
            }
            MHD_websocket_stream_free (cu->ws);
            free (cu->extra_in);
            free (cu);
            return NULL;
        }
        free (cu->extra_in);
        cu->extra_in = NULL;
    }

    /* the main loop for receiving data */
    while (1)
    {
        char buf[128];
        ssize_t got = recv (cu->fd,
                            buf,
                            sizeof (buf),
                            0);
        if (0 >= got)
        {
            /* the TCP/IP socket has been closed */
            break;
        }
        if (0 < got)
        {
            if (0 != connecteduser_parse_received_websocket_stream (cu, buf,
                                                                    (size_t) got))
            {
                /* A websocket protocol error occurred */
                cu->disconnect_ = true;
                thread.join();
                struct MHD_UpgradeResponseHandle*urh = cu->urh;
                if (NULL != urh)
                {
                    cu->urh = NULL;
                    MHD_upgrade_action (urh,
                                        MHD_UPGRADE_ACTION_CLOSE);
                }
                MHD_websocket_stream_free (cu->ws);
                free (cu);
                return NULL;
            }
        }
    }

    /* cleanup */
    cu->disconnect_ = true;
    thread.join();
    struct MHD_UpgradeResponseHandle* urh = cu->urh;
    if (NULL != urh)
    {
        cu->urh = NULL;
        MHD_upgrade_action (urh,
                            MHD_UPGRADE_ACTION_CLOSE);
    }
    MHD_websocket_stream_free (cu->ws);
    free (cu);

    return NULL;
}

/**
 * Function called after a protocol "upgrade" response was sent
 * successfully and the socket should now be controlled by some
 * protocol other than HTTP.
 *
 * Any data already received on the socket will be made available in
 * @e extra_in.  This can happen if the application sent extra data
 * before MHD send the upgrade response.  The application should
 * treat data from @a extra_in as if it had read it from the socket.
 *
 * Note that the application must not close() @a sock directly,
 * but instead use #MHD_upgrade_action() for special operations
 * on @a sock.
 *
 * Data forwarding to "upgraded" @a sock will be started as soon
 * as this function return.
 *
 * Except when in 'thread-per-connection' mode, implementations
 * of this function should never block (as it will still be called
 * from within the main event loop).
 *
 * @param cls closure, whatever was given to #MHD_create_response_for_upgrade().
 * @param connection original HTTP connection handle,
 *                   giving the function a last chance
 *                   to inspect the original HTTP request
 * @param con_cls last value left in `con_cls` of the `MHD_AccessHandlerCallback`
 * @param extra_in if we happened to have read bytes after the
 *                 HTTP header already (because the client sent
 *                 more than the HTTP header of the request before
 *                 we sent the upgrade response),
 *                 these are the extra bytes already read from @a sock
 *                 by MHD.  The application should treat these as if
 *                 it had read them from @a sock.
 * @param extra_in_size number of bytes in @a extra_in
 * @param sock socket to use for bi-directional communication
 *        with the client.  For HTTPS, this may not be a socket
 *        that is directly connected to the client and thus certain
 *        operations (TCP-specific setsockopt(), getsockopt(), etc.)
 *        may not work as expected (as the socket could be from a
 *        socketpair() or a TCP-loopback).  The application is expected
 *        to perform read()/recv() and write()/send() calls on the socket.
 *        The application may also call shutdown(), but must not call
 *        close() directly.
 * @param urh argument for #MHD_upgrade_action()s on this @a connection.
 *        Applications must eventually use this callback to (indirectly)
 *        perform the close() action on the @a sock.
 */
void webserver::upgrade_handler (void *cls,
                 struct MHD_Connection *connection,
                 void *con_cls,
                 const char *extra_in,
                 size_t extra_in_size,
                 MHD_socket fd,
                 struct MHD_UpgradeResponseHandle *urh)
{
    pthread_t pt;
    (void) connection;  /* Unused. Silent compiler warning. */
    (void) con_cls;     /* Unused. Silent compiler warning. */

    /* This callback must return as soon as possible. */

    /* allocate new connected user */
    websocket* cu = new websocket();
    if (0 != extra_in_size)
    {
        cu->extra_in = (char*)malloc (extra_in_size);
        if (NULL == cu->extra_in)
            abort ();
        memcpy (cu->extra_in,
                extra_in,
                extra_in_size);
    }
    cu->extra_in_size = extra_in_size;
    cu->fd = fd;
    cu->urh = urh;
    cu->ws_handler = (websocket_handler*)cls;

    /* create thread for the new connected user */
    if (0 != pthread_create (&pt,
                            NULL,
                            &connecteduser_receive_messages,
                            cu))
        abort ();
    pthread_detach (pt);
}

MHD_Result webserver::create_websocket_connection(
    websocket_handler* ws_handler,
    MHD_Connection *connection)
{
    /**
     * The path for the chat has been accessed.
     * For a valid WebSocket request, at least five headers are required:
     * 1. "Host: <name>"
     * 2. "Connection: Upgrade"
     * 3. "Upgrade: websocket"
     * 4. "Sec-WebSocket-Version: 13"
     * 5. "Sec-WebSocket-Key: <base64 encoded value>"
     * Values are compared in a case-insensitive manner.
     * Furthermore it must be a HTTP/1.1 or higher GET request.
     * See: https://tools.ietf.org/html/rfc6455#section-4.2.1
     *
     * To ease this example we skip the following checks:
     * - Whether the HTTP version is 1.1 or newer
     * - Whether Connection is Upgrade, because this header may
     *   contain multiple values.
     * - The requested Host (because we don't know)
     */

    MHD_Result ret;

    /* check whether a websocket upgrade is requested */
    const char*value = MHD_lookup_connection_value (connection,
                                                    MHD_HEADER_KIND,
                                                    MHD_HTTP_HEADER_UPGRADE);
    if ((0 == value) || (0 != strcasecmp (value, "websocket")))
    {
        struct MHD_Response*response = MHD_create_response_from_buffer (strlen (
                                                                            PAGE_INVALID_WEBSOCKET_REQUEST),
                                                                        (void*)PAGE_INVALID_WEBSOCKET_REQUEST,
                                                                        MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response (connection,
                                    MHD_HTTP_BAD_REQUEST,
                                    response);
        MHD_destroy_response (response);
        return ret;
    }

    /* check the protocol version */
    value = MHD_lookup_connection_value (connection,
                                         MHD_HEADER_KIND,
                                         MHD_HTTP_HEADER_SEC_WEBSOCKET_VERSION);
    if ((0 == value) || (0 != strcasecmp (value, "13")))
    {
        struct MHD_Response*response = MHD_create_response_from_buffer (strlen (
                                                                            PAGE_INVALID_WEBSOCKET_REQUEST),
                                                                        (void*)PAGE_INVALID_WEBSOCKET_REQUEST,
                                                                        MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response (connection,
                                    MHD_HTTP_BAD_REQUEST,
                                    response);
        MHD_destroy_response (response);
        return ret;
    }

    /* read the websocket key (required for the response) */
    value = MHD_lookup_connection_value (connection, MHD_HEADER_KIND,
                                         MHD_HTTP_HEADER_SEC_WEBSOCKET_KEY);
    if (0 == value)
    {
        struct MHD_Response*response = MHD_create_response_from_buffer (strlen (
                                                                            PAGE_INVALID_WEBSOCKET_REQUEST),
                                                                        (void*)PAGE_INVALID_WEBSOCKET_REQUEST,
                                                                        MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response (connection,
                                    MHD_HTTP_BAD_REQUEST,
                                    response);
        MHD_destroy_response (response);
        return ret;
    }

    /* generate the response accept header */
    char sec_websocket_accept[29];
    if (0 != MHD_websocket_create_accept (value, sec_websocket_accept))
    {
        struct MHD_Response*response = MHD_create_response_from_buffer (strlen (
                                                                            PAGE_INVALID_WEBSOCKET_REQUEST),
                                                                        (void*)PAGE_INVALID_WEBSOCKET_REQUEST,
                                                                        MHD_RESPMEM_PERSISTENT);
        ret = MHD_queue_response (connection,
                                    MHD_HTTP_BAD_REQUEST,
                                    response);
        MHD_destroy_response (response);
        return ret;
    }

    /* create the response for upgrade */
    MHD_Response* response = MHD_create_response_for_upgrade(
        &upgrade_handler, ws_handler);

    /**
     * For the response we need at least the following headers:
     * 1. "Connection: Upgrade"
     * 2. "Upgrade: websocket"
     * 3. "Sec-WebSocket-Accept: <base64value>"
     * The value for Sec-WebSocket-Accept can be generated with MHD_websocket_create_accept.
     * It requires the value of the Sec-WebSocket-Key header of the request.
     * See also: https://tools.ietf.org/html/rfc6455#section-4.2.2
     */
    MHD_add_response_header (response,
                             MHD_HTTP_HEADER_CONNECTION,
                             "Upgrade");
    MHD_add_response_header (response,
                             MHD_HTTP_HEADER_UPGRADE,
                             "websocket");
    MHD_add_response_header (response,
                             MHD_HTTP_HEADER_SEC_WEBSOCKET_ACCEPT,
                             sec_websocket_accept);
    ret = MHD_queue_response (connection,
                              MHD_HTTP_SWITCHING_PROTOCOLS,
                              response);
    MHD_destroy_response (response);
    return ret;
}

MHD_Result webserver::finalize_answer(MHD_Connection* connection, struct details::modded_request* mr, const char* method) {
    int to_ret = MHD_NO;

    map<string, http_resource*>::iterator fe;

    http_resource* hrm;

    bool found = false;
    struct MHD_Response* raw_response;
    if (!single_resource) {
        const char* st_url = mr->standardized_url->c_str();
        fe = registered_resources_str.find(st_url);
        if (fe == registered_resources_str.end()) {
            if (regex_checking) {
                map<details::http_endpoint, http_resource*>::iterator found_endpoint;

                details::http_endpoint endpoint(st_url, false, false, false);

                map<details::http_endpoint, http_resource*>::iterator it;

                size_t len = 0;
                size_t tot_len = 0;
                for (it = registered_resources.begin(); it != registered_resources.end(); ++it) {
                    size_t endpoint_pieces_len = (*it).first.get_url_pieces().size();
                    size_t endpoint_tot_len = (*it).first.get_url_complete().size();
                    if (!found || endpoint_pieces_len > len || (endpoint_pieces_len == len && endpoint_tot_len > tot_len)) {
                        if ((*it).first.match(endpoint)) {
                            found = true;
                            len = endpoint_pieces_len;
                            tot_len = endpoint_tot_len;
                            found_endpoint = it;
                        }
                    }
                }

                if (found) {
                    vector<string> url_pars = found_endpoint->first.get_url_pars();

                    vector<string> url_pieces = endpoint.get_url_pieces();
                    vector<int> chunks = found_endpoint->first.get_chunk_positions();
                    for (unsigned int i = 0; i < url_pars.size(); i++) {
                        mr->dhr->set_arg(url_pars[i], url_pieces[chunks[i]]);
                    }

                    hrm = found_endpoint->second;
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

    if (found) {
        try {
            if (hrm->is_allowed(method)) {
                bool is_websocket = false;
                if (mr->callback == &http_resource::render_GET) {
                    const char* value = MHD_lookup_connection_value (connection,
                                                                     MHD_HEADER_KIND,
                                                                     MHD_HTTP_HEADER_UPGRADE);
                    is_websocket = ((0 != value) && (0 == strcasecmp (value, "websocket")));
                }
                if (is_websocket) {
                    websocket_handler* ws_handler = dynamic_cast<websocket_handler*>(hrm);
                    if (ws_handler == nullptr) {
                        mr->dhrs = internal_error_page(mr);
                    } else {
                        return create_websocket_connection(ws_handler, connection);
                    }
                } else {
                    mr->dhrs = ((hrm)->*(mr->callback))(*mr->dhr);  // copy in memory (move in case)
                    if (mr->dhrs->get_response_code() == -1) {
                        mr->dhrs = internal_error_page(mr);
                    }
                }
            } else {
                mr->dhrs = method_not_allowed_page(mr);
            }
        } catch(const std::exception& e) {
            mr->dhrs = internal_error_page(mr);
        } catch(...) {
            mr->dhrs = internal_error_page(mr);
        }
    } else {
        mr->dhrs = not_found_page(mr);
    }

    try {
        try {
            raw_response = mr->dhrs->get_raw_response();
        } catch(const std::invalid_argument& iae) {
            mr->dhrs = not_found_page(mr);
            raw_response = mr->dhrs->get_raw_response();
        } catch(const std::exception& e) {
            mr->dhrs = internal_error_page(mr);
            raw_response = mr->dhrs->get_raw_response();
        } catch(...) {
            mr->dhrs = internal_error_page(mr);
            raw_response = mr->dhrs->get_raw_response();
        }
    } catch(...) {  // catches errors in internal error page
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

    mr->dhr->set_path(mr->standardized_url->c_str());
    mr->dhr->set_method(method);
    mr->dhr->set_version(version);

    return finalize_answer(connection, mr, method);
}

MHD_Result webserver::answer_to_connection(void* cls, MHD_Connection* connection, const char* url, const char* method,
        const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls) {
    struct details::modded_request* mr = static_cast<struct details::modded_request*>(*con_cls);

    if (mr->second != false) {
        return static_cast<webserver*>(cls)->requests_answer_second_step(connection, method, version, upload_data, upload_data_size, mr);
    }

    const MHD_ConnectionInfo * conninfo = MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CONNECTION_FD);

    if (static_cast<webserver*>(cls)->tcp_nodelay) {
        int yes = 1;
        setsockopt(conninfo->connect_fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&yes), sizeof(int));
    }

    std::string t_url = url;

    base_unescaper(&t_url, static_cast<webserver*>(cls)->unescaper);
    mr->standardized_url = new string(http_utils::standardize_url(t_url));

    mr->has_body = false;

    access_log(static_cast<webserver*>(cls), *(mr->complete_uri) + " METHOD: " + method);

    if (0 == strcasecmp(method, http_utils::http_method_get)) {
        mr->callback = &http_resource::render_GET;
    } else if (0 == strcmp(method, http_utils::http_method_post)) {
        mr->callback = &http_resource::render_POST;
        mr->has_body = true;
    } else if (0 == strcasecmp(method, http_utils::http_method_put)) {
        mr->callback = &http_resource::render_PUT;
        mr->has_body = true;
    } else if (0 == strcasecmp(method, http_utils::http_method_delete)) {
        mr->callback = &http_resource::render_DELETE;
        mr->has_body = true;
    } else if (0 == strcasecmp(method, http_utils::http_method_patch)) {
        mr->callback = &http_resource::render_PATCH;
        mr->has_body = true;
    } else if (0 == strcasecmp(method, http_utils::http_method_head)) {
        mr->callback = &http_resource::render_HEAD;
    } else if (0 ==strcasecmp(method, http_utils::http_method_connect)) {
        mr->callback = &http_resource::render_CONNECT;
    } else if (0 == strcasecmp(method, http_utils::http_method_trace)) {
        mr->callback = &http_resource::render_TRACE;
    } else if (0 ==strcasecmp(method, http_utils::http_method_options)) {
        mr->callback = &http_resource::render_OPTIONS;
    }

    return static_cast<webserver*>(cls)->requests_answer_first_step(connection, mr);
}

}  // namespace httpserver
