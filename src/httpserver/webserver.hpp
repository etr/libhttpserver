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

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/create_webserver.hpp"

// TASK-020: <sys/socket.h> is deliberately NOT included from this public
// header. The class below uses `struct sockaddr` and `struct sockaddr_storage`
// only by pointer, so they are forward-declared at file scope. The two
// public methods that take socket-layer types on their argument lists --
// `get_fdset(...)` and `add_connection(...)` -- accept opaque scalar types
// (`void*` for `fd_set*`, `unsigned int` for `socklen_t`) on the public
// surface so callers do not have to drag in `<sys/select.h>` or
// `<sys/socket.h>` to use the umbrella `<httpserver.hpp>` header. The
// implementations in src/webserver.cpp cast back to the real types where
// the BSD-socket headers are reachable directly. POSIX guarantees
// `socklen_t` is an unsigned integral type of at least 32 bits, and
// `unsigned int` is wider on every supported platform.
struct sockaddr;
struct sockaddr_storage;

// Forward declarations: backend (MHD) types are intentionally NOT pulled in.
// libmicrohttpd's <microhttpd.h> and <pthread.h> live behind the PIMPL
// boundary in detail/webserver_impl.hpp (TASK-014).
namespace httpserver {
class http_resource;
class http_response;
#ifdef HAVE_WEBSOCKET
class websocket_handler;
#endif  // HAVE_WEBSOCKET
namespace detail {
struct modded_request;
class webserver_impl;
}  // namespace detail
}  // namespace httpserver

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
     // PIMPL-owned: copy/move would slice the backing impl object.
     webserver(const webserver&) = delete;
     webserver& operator=(const webserver&) = delete;
     webserver(webserver&&) = delete;
     webserver& operator=(webserver&&) = delete;
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
      * Register a resource for an exact (non-prefix) URL match.
      *
      * The path matches only itself, including any parameterized form:
      * `register_path("/users/{id}")` matches `/users/42` but NOT
      * `/users/42/profile`.
      *
      * Templated unique_ptr<T> shim funnels into the shared_ptr overload
      * exactly the way TASK-023's pattern does, so calls with derived
      * types resolve unambiguously.
      *
      * Throws std::invalid_argument if the resource pointer is null,
      * if the path conflicts with single_resource mode (single_resource
      * requires register_prefix), or if a resource is already
      * registered at the same path.
      *
      * @param path The url pointing to the resource. May be parameterized
      *             in the form /path/to/url/{par1}/and/{par2} or a regex.
      * @param res  unique_ptr to the http_resource (or any derived type);
      *             ownership is transferred to the webserver.
     **/
     template <typename T,
               typename = std::enable_if_t<
                   std::is_base_of_v<http_resource, T>>>
     void register_path(const std::string& path, std::unique_ptr<T> res) {
         register_path(path,
                       std::shared_ptr<http_resource>(std::move(res)));
     }
     /// @copydoc register_path(const std::string&, std::unique_ptr<T>)
     /// @param res shared_ptr to the http_resource; the caller retains a reference.
     void register_path(const std::string& path,
                        std::shared_ptr<http_resource> res);

     /**
      * Register a resource for a prefix URL match (the path and all its
      * children match).
      *
      * `register_prefix("/static")` matches `/static`, `/static/x`,
      * `/static/anything/here`, etc.
      *
      * Templated unique_ptr<T> shim funnels into the shared_ptr overload
      * exactly the way TASK-023's pattern does.
      *
      * Throws std::invalid_argument if the resource pointer is null,
      * or if a resource is already registered at the same path.
      *
      * @param path The url whose subtree this resource handles.
      * @param res  unique_ptr to the http_resource (or any derived type).
     **/
     template <typename T,
               typename = std::enable_if_t<
                   std::is_base_of_v<http_resource, T>>>
     void register_prefix(const std::string& path, std::unique_ptr<T> res) {
         register_prefix(path,
                         std::shared_ptr<http_resource>(std::move(res)));
     }
     /// @copydoc register_prefix(const std::string&, std::unique_ptr<T>)
     /// @param res shared_ptr to the http_resource; the caller retains a reference.
     void register_prefix(const std::string& path,
                          std::shared_ptr<http_resource> res);

     /**
      * Deprecated alias for register_path. Forwards to register_path so
      * existing TASK-023-era call sites keep compiling. The 3-arg
      * `bool family` overload from before TASK-024 has been removed --
      * use register_prefix() for prefix matching instead.
      *
      * @param path The url pointing to the resource.
      * @param res  unique_ptr to the http_resource (or any derived type).
     **/
     template <typename T,
               typename = std::enable_if_t<
                   std::is_base_of_v<http_resource, T>>>
     [[deprecated("use register_path() for exact match or register_prefix() for prefix match")]]
     void register_resource(const std::string& path, std::unique_ptr<T> res) {
         register_path(path, std::move(res));
     }
     /// @copydoc register_resource(const std::string&, std::unique_ptr<T>)
     [[deprecated("use register_path() for exact match or register_prefix() for prefix match")]]
     void register_resource(const std::string& path,
                            std::shared_ptr<http_resource> res);

     /**
      * Unregister an exact-match (register_path) registration.
      * No-op if no exact registration exists at @p path.
     **/
     void unregister_path(const std::string& path);

     /**
      * Unregister a prefix-match (register_prefix) registration.
      * No-op if no prefix registration exists at @p path.
     **/
     void unregister_prefix(const std::string& path);

     /**
      * Kind-agnostic convenience: erases either an exact or a prefix
      * registration at @p path. Equivalent to calling unregister_path
      * and unregister_prefix; idempotent.
     **/
     void unregister_resource(const std::string& path);
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
      * The three set parameters are typed as `void*` so this header does not
      * have to include `<sys/select.h>` (which transitively pulls in
      * `<sys/socket.h>` on most platforms). Callers MUST pass valid
      * `fd_set*` pointers; the implementation casts back to `fd_set*`
      * internally. Conversion from `fd_set*` to `void*` is implicit at
      * the call site, so existing source-level callers compile unchanged.
      * @param read_fd_set set of FDs to watch for reading (`fd_set*`)
      * @param write_fd_set set of FDs to watch for writing (`fd_set*`)
      * @param except_fd_set set of FDs to watch for exceptions (`fd_set*`)
      * @param max_fd highest FD number set in any of the sets
      * @return true on success, false on error
     **/
     bool get_fdset(void* read_fd_set, void* write_fd_set, void* except_fd_set, int* max_fd);

     /**
      * Get the timeout until the next MHD action is needed.
      * @param timeout output: timeout in milliseconds
      * @return true if a timeout was set, false if no timeout is needed
     **/
     bool get_timeout(uint64_t* timeout);

     /**
      * Add an externally-accepted socket connection.
      * `addrlen` is typed as `unsigned int` rather than `socklen_t` so
      * this header does not have to include `<sys/socket.h>`. POSIX
      * guarantees `socklen_t` is an unsigned integer of at least 32 bits;
      * `unsigned int` is wider on every supported platform. The
      * implementation in src/webserver.cpp passes the value directly to
      * MHD_add_connection, which takes a `socklen_t`.
      * @param client_socket the accepted client socket
      * @param addr the client address (forward-declared `struct sockaddr*`)
      * @param addrlen length of the address (in bytes)
      * @return true on success, false on error
     **/
     bool add_connection(int client_socket, const struct sockaddr* addr, unsigned int addrlen);

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

     // TASK-024: shared registration helper. Both register_path and
     // register_prefix funnel through here so the validation/insertion
     // logic lives in one place. `family=true` is prefix-matching;
     // `family=false` is exact-matching.
     void register_impl_(const std::string& path,
                         std::shared_ptr<http_resource> res,
                         bool family);

     // TASK-024: shared unregistration helper. Erases a single
     // registration of the requested kind.
     void unregister_impl_(const std::string& path, bool family);

     // PIMPL: backend-coupled state (MHD daemon, pthread mutexes, route
     // table, ban set, route cache, websocket registry, GnuTLS SNI cache,
     // and the dispatch helpers / MHD trampolines that operate on those)
     // lives behind this pointer in detail/webserver_impl.hpp. The public
     // header carries no <microhttpd.h>/<pthread.h>/<gnutls/...> baggage.
     std::unique_ptr<detail::webserver_impl> impl_;

     // detail::webserver_impl reads the const config bag above (tcp_nodelay,
     // unescaper, regex_checking, auth_handler, etc.) when servicing
     // requests, and houses the MHD trampolines / dispatch helpers so
     // <microhttpd.h> stays out of this public header. Granting friendship
     // is preferable to introducing a long list of trivial public getters
     // that cross the PIMPL boundary in both directions.
     friend class detail::webserver_impl;
     friend class http_response;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_WEBSERVER_HPP_
