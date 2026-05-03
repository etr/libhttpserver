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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_WEBSERVER_HPP_
#define SRC_HTTPSERVER_WEBSERVER_HPP_

#include <microhttpd.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#if !defined(__MINGW32__)
#include <sys/socket.h>
#endif

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif  // HAVE_GNUTLS

#include "httpserver/constants.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/create_webserver.hpp"
#include "httpserver/detail/http_endpoint.hpp"

namespace httpserver { class http_resource; }
namespace httpserver { class http_response; }
#ifdef HAVE_WEBSOCKET
namespace httpserver { class websocket_handler; }
#endif  // HAVE_WEBSOCKET
namespace httpserver { namespace detail { struct modded_request; } }

struct MHD_Connection;

namespace httpserver {

/**
 * Class representing the webserver. Main class of the apis.
**/
class webserver {
 public:
     // Keeping this non explicit on purpose to easy construction through builder class.
     webserver(const create_webserver& params);  // NOLINT(runtime/explicit)
     /**
      * Destructor of the class
     **/
     ~webserver();
     /**
      * Method used to start the webserver.
      * This method can be blocking or not.
      * @param blocking param indicating if the method is blocking or not
      * @return a boolean indicating if the webserver is running or not.
     **/
     bool start(bool blocking = false);
     /**
      * Method used to stop the webserver.
      * @return true if the webserver is stopped.
     **/
     bool stop();
     /**
      * Method used to evaluate if the server is running or not.
      * @return true if the webserver is running
     **/
     bool is_running();
     /**
      * Method used to register a resource with the webserver.
      * @param resource The url pointing to the resource. This url could be also parametrized in the form /path/to/url/{par1}/and/{par2}
      *                 or a regular expression.
      * @param http_resource http_resource pointer to register.
      * @param family boolean indicating whether the resource is registered for the endpoint and its child or not.
      * @return true if the resource was registered
     **/
     bool register_resource(const std::string& resource, http_resource* res, bool family = false);

     void unregister_resource(const std::string& resource);
     void ban_ip(const std::string& ip);
     void allow_ip(const std::string& ip);
     void unban_ip(const std::string& ip);
     void disallow_ip(const std::string& ip);

     log_access_ptr get_access_logger() const {
         return log_access;
     }

     log_error_ptr get_error_logger() const {
         return log_error;
     }

     validator_ptr get_request_validator() const {
         return validator;
     }

     unescaper_ptr get_unescaper() const {
         return unescaper;
     }

     /**
      * Method used to kill the webserver waiting for it to terminate
     **/
     void sweet_kill();

     /**
      * Run the webserver's event loop once (non-blocking).
      * For use with external event loops when the server is started
      * without internal threading.
      * @return true on success, false on error
     **/
     bool run();

     /**
      * Run the webserver's event loop, blocking until there is activity
      * or the timeout expires.
      * @param millisec timeout in milliseconds (-1 for indefinite)
      * @return true on success, false on error
     **/
     bool run_wait(int32_t millisec);

     /**
      * Get the file descriptor sets for select()-based event loop integration.
      * @param read_fd_set set of FDs to watch for reading
      * @param write_fd_set set of FDs to watch for writing
      * @param except_fd_set set of FDs to watch for exceptions
      * @param max_fd highest FD number set in any of the sets
      * @return true on success, false on error
     **/
     bool get_fdset(fd_set* read_fd_set, fd_set* write_fd_set, fd_set* except_fd_set, int* max_fd);

     /**
      * Get the timeout until the next MHD action is needed.
      * @param timeout output: timeout in milliseconds
      * @return true if a timeout was set, false if no timeout is needed
     **/
     bool get_timeout(uint64_t* timeout);

     /**
      * Add an externally-accepted socket connection.
      * @param client_socket the accepted client socket
      * @param addr the client address
      * @param addrlen length of the address
      * @return true on success, false on error
     **/
     bool add_connection(int client_socket, const struct sockaddr* addr, socklen_t addrlen);

     /**
      * Quiesce the daemon: stop accepting new connections while letting
      * in-flight requests complete.
      * @return the listen socket FD (caller can close it), or -1 on error
     **/
     int quiesce();

     /**
      * Get the listen socket file descriptor.
      * @return the listen FD, or -1 if not available
     **/
     int get_listen_fd() const;

     /**
      * Get the number of currently active connections.
      * @return active connection count
     **/
     unsigned int get_active_connections() const;

     /**
      * Get the actual port the daemon is bound to.
      * Useful when port 0 was specified to let the OS choose.
      * @return the bound port, or 0 if not available
     **/
     uint16_t get_bound_port() const;

#ifdef HAVE_WEBSOCKET
     bool register_ws_resource(const std::string& resource, websocket_handler* handler);
#endif  // HAVE_WEBSOCKET

 protected:
     webserver& operator=(const webserver& other);

 private:
     const uint16_t port;
     http::http_utils::start_method_T start_method;
     const int max_threads;
     const int max_connections;
     const int memory_limit;
     const size_t content_size_limit;
     const int connection_timeout;
     const int per_IP_connection_limit;
     log_access_ptr log_access;
     log_error_ptr log_error;
     validator_ptr validator;
     unescaper_ptr unescaper;
     const struct sockaddr* bind_address;
     std::shared_ptr<struct sockaddr_storage> bind_address_storage;
     /* Changed type to MHD_socket because this type will always reflect the
     platform's actual socket type (e.g. SOCKET on windows, int on unixes)*/
     MHD_socket bind_socket;
     const int max_thread_stack_size;
     const bool use_ssl;
     const bool use_ipv6;
     const bool use_dual_stack;
     const bool debug;
     const bool pedantic;
     const std::string https_mem_key;
     const std::string https_mem_cert;
     const std::string https_mem_trust;
     const std::string https_priorities;
     const http::http_utils::cred_type_T cred_type;
     const psk_cred_handler_callback psk_cred_handler;
     const std::string digest_auth_random;
     const int nonce_nc_size;
     bool running;
     const http::http_utils::policy_T default_policy;
#ifdef HAVE_BAUTH
     const bool basic_auth_enabled;
#endif  // HAVE_BAUTH
     const bool digest_auth_enabled;
     const bool regex_checking;
     const bool ban_system_enabled;
     const bool post_process_enabled;
     const bool put_processed_data_to_content;
     const file_upload_target_T file_upload_target;
     const std::string file_upload_dir;
     const bool generate_random_filename_on_upload;
     const bool deferred_enabled;
     const bool single_resource;
     const bool tcp_nodelay;
     pthread_mutex_t mutexwait;
     pthread_cond_t mutexcond;
     const render_ptr not_found_resource;
     const render_ptr method_not_allowed_resource;
     const render_ptr internal_error_resource;
     const file_cleanup_callback_ptr file_cleanup_callback;
     const auth_handler_ptr auth_handler;
     const std::vector<std::string> auth_skip_paths;
     const sni_callback_t sni_callback;
     const bool no_listen_socket;
     const bool no_thread_safety;
     const bool turbo;
     const bool suppress_date_header;
     const int listen_backlog;
     const int address_reuse;
     const size_t connection_memory_increment;
     const int tcp_fastopen_queue_size;
     const bool sigpipe_handled_by_app;
     const std::string https_mem_dhparams;
     const std::string https_key_password;
     const std::string https_priorities_append;
     const bool no_alpn;
     const int client_discipline_level;
     std::shared_mutex registered_resources_mutex;
     std::map<detail::http_endpoint, http_resource*> registered_resources;
     std::map<std::string, http_resource*> registered_resources_str;
     std::map<detail::http_endpoint, http_resource*> registered_resources_regex;

     struct route_cache_entry {
         detail::http_endpoint matched_endpoint;
         http_resource* resource;
     };
     static constexpr size_t ROUTE_CACHE_MAX_SIZE = 256;
     std::mutex route_cache_mutex;
     std::list<std::pair<std::string, route_cache_entry>> route_cache_list;
     std::unordered_map<std::string, std::list<std::pair<std::string, route_cache_entry>>::iterator> route_cache_map;

     std::shared_mutex bans_mutex;
     std::set<http::ip_representation> bans;

     std::shared_mutex allowances_mutex;
     std::set<http::ip_representation> allowances;

     struct MHD_Daemon* daemon;

#ifdef HAVE_WEBSOCKET
     std::map<std::string, websocket_handler*> registered_ws_handlers;
#endif  // HAVE_WEBSOCKET

     std::shared_ptr<http_response> method_not_allowed_page(detail::modded_request* mr) const;
     std::shared_ptr<http_response> internal_error_page(detail::modded_request* mr, bool force_our = false) const;
     std::shared_ptr<http_response> not_found_page(detail::modded_request* mr) const;
     bool should_skip_auth(const std::string& path) const;

     static void request_completed(void *cls,
             struct MHD_Connection *connection, void **con_cls,
             enum MHD_RequestTerminationCode toe);

     static MHD_Result answer_to_connection(void* cls, MHD_Connection* connection, const char* url,
             const char* method, const char* version, const char* upload_data,
             size_t* upload_data_size, void** con_cls);

     static MHD_Result post_iterator(void *cls, enum MHD_ValueKind kind, const char *key,
             const char *filename, const char *content_type, const char *transfer_encoding,
             const char *data, uint64_t off, size_t size);

#ifdef HAVE_WEBSOCKET
     struct ws_upgrade_data {
         webserver* ws;
         websocket_handler* handler;
     };

     static void upgrade_handler(void *cls, struct MHD_Connection* connection,
                                 void *req_cls, const char *extra_in,
                                 size_t extra_in_size, MHD_socket sock,
                                 struct MHD_UpgradeResponseHandle *urh);
#endif  // HAVE_WEBSOCKET

     MHD_Result requests_answer_first_step(MHD_Connection* connection, struct detail::modded_request* mr);

     MHD_Result requests_answer_second_step(MHD_Connection* connection,
             const char* method, const char* version, const char* upload_data,
             size_t* upload_data_size, struct detail::modded_request* mr);

     MHD_Result finalize_answer(MHD_Connection* connection, struct detail::modded_request* mr, const char* method);

     struct MHD_Response* get_raw_response_with_fallback(detail::modded_request* mr);

     MHD_Result complete_request(MHD_Connection* connection, struct detail::modded_request* mr, const char* version, const char* method);

     void invalidate_route_cache();

#ifdef HAVE_GNUTLS
     // MHD_PskServerCredentialsCallback signature
     static int psk_cred_handler_func(void* cls,
                                       struct MHD_Connection* connection,
                                       const char* username,
                                       void** psk,
                                       size_t* psk_size);

#ifdef MHD_OPTION_HTTPS_CERT_CALLBACK
     // SNI certificate callback function (libmicrohttpd 0.9.71+)
     static int sni_cert_callback_func(void* cls,
                                        struct MHD_Connection* connection,
                                        const char* server_name,
                                        gnutls_certificate_credentials_t* creds);

     // Cache for loaded credentials per server name
     mutable std::map<std::string, gnutls_certificate_credentials_t> sni_credentials_cache;
     mutable std::shared_mutex sni_credentials_mutex;
#endif  // MHD_OPTION_HTTPS_CERT_CALLBACK
#endif  // HAVE_GNUTLS

     friend MHD_Result policy_callback(void *cls, const struct sockaddr* addr, socklen_t addrlen);
     friend void error_log(void* cls, const char* fmt, va_list ap);
     friend void access_log(webserver* cls, std::string uri);
     friend void* uri_log(void* cls, const char* uri);
     friend size_t unescaper_func(void * cls, struct MHD_Connection *c, char *s);
     friend class http_response;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_WEBSERVER_HPP_
