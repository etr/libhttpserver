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

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_method.hpp"
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
// TASK-034: forward-declared unconditionally so the public surface of
// webserver is identical in HAVE_WEBSOCKET-on and HAVE_WEBSOCKET-off
// builds (PRD-FLG-REQ-001). When HAVE_WEBSOCKET is undefined the
// class definition in websocket_handler.hpp is still included via the
// umbrella header; member-function bodies live in src/websocket_handler.cpp.
class websocket_handler;
namespace detail {
struct modded_request;
class webserver_impl;
}  // namespace detail
}  // namespace httpserver

namespace httpserver {

/**
 * Class representing the webserver. Main class of the apis.
 *
 * ### Threading contract (DR-008 / §5.1)
 *
 * The webserver dispatches each request on one of libmicrohttpd's worker
 * threads. The thread-safety contract:
 *
 *   1. Public registration / un-registration methods (@ref register_path,
 *      @ref register_prefix, @ref register_resource, the @ref on_get
 *      family, @ref route, @ref unregister_path, @ref unregister_prefix,
 *      @ref unregister_resource, @ref register_ws_resource,
 *      @ref unregister_ws_resource, @ref block_ip, @ref unblock_ip) are
 *      thread-safe and re-entrant from inside a request handler.
 *   2. The exceptions are @ref stop, @ref stop_and_wait, and the
 *      destructor: each joins libmicrohttpd's worker threads and
 *      therefore deadlocks (or aborts with "Failed to join a thread."
 *      on some libmicrohttpd versions) when called from within a
 *      handler thread. Call them from the thread that owns the
 *      webserver instance.
 *   3. `http_request` is single-threaded per request: it is owned by
 *      the worker thread servicing that request and MUST NOT be
 *      retained beyond the handler's return.
 *   4. `http_response` is a value type with exclusive ownership; no
 *      cross-thread sharing.
 *   5. User-supplied callbacks invoked from MHD worker threads --
 *      @ref create_webserver::log_access, @ref create_webserver::log_error,
 *      @ref create_webserver::not_found_handler,
 *      @ref create_webserver::method_not_allowed_handler,
 *      @ref create_webserver::internal_error_handler,
 *      @ref create_webserver::file_cleanup_callback, the PSK / SNI / ALPN
 *      callbacks, and any registered @ref http_resource render method --
 *      may run concurrently on multiple threads. Implementations MUST be
 *      thread-safe.
 *
 * See specs/architecture/11-decisions/DR-008.md and §5.1 for the
 * decision record.
 *
 * ### Handler error-propagation contract (DR-009 / §5.2 / PRD-FLG-REQ-002)
 *
 * Every registered request handler is invoked from the dispatch path under
 * a two-branch try/catch. The contract:
 *
 *   1. The handler call is wrapped in
 *      `try { ... } catch (const std::exception& e) { ... } catch (...) { ... }`.
 *   2. On `std::exception`: the message is logged via the configured
 *      `log_error` callback, then `internal_error_handler` is invoked with
 *      `e.what()`. The response it returns is sent on the wire (default
 *      500 with the message in the body when no handler is configured).
 *   3. On non-`std::exception` (e.g. `throw 42`): same path with the
 *      message replaced by the literal string `"unknown exception"`.
 *   4. If `internal_error_handler` itself throws while servicing 2 or 3,
 *      the failure is logged generically and a hardcoded 500 with an
 *      EMPTY body is sent. No exception ever escapes into libmicrohttpd.
 *   5. `feature_unavailable` (a `std::runtime_error` subclass) is NOT
 *      mapped to a special status: it lands as a generic 500 like any
 *      other `std::exception`.
 *   6. The `log_error` callback may be invoked concurrently from multiple
 *      MHD worker threads; user implementations MUST be thread-safe.
 *   7. Hook layering (DR-012 §4.10):
 *      @ref hook_phase::handler_exception hooks fire BEFORE this alias;
 *      throwing hooks are caught and the chain continues; (4) fires
 *      without re-invoking the alias on full chain failure.
 *
 * Resources are encouraged to throw rather than synthesise 500s.
**/
class webserver {
 public:
     // PRD-NAM-REQ-004: explicit to forbid implicit conversion from
     // create_webserver. Callers must direct-init: webserver ws{cw};
     explicit webserver(const create_webserver& params);
     /**
      * Destructor.
      *
      * Calls stop() unconditionally, which joins libmicrohttpd's
      * worker threads. For the same reason as stop(), destroying a
      * webserver from inside a handler thread deadlocks (or, on some
      * libmicrohttpd versions, aborts with "Failed to join a thread.").
      * Destroy the webserver from the thread that constructed it.
      *
      * See specs/architecture/11-decisions/DR-008.md and §5.1.
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
      * Stop the webserver.
      *
      * Joins libmicrohttpd's worker threads before returning. Safe to
      * call from any thread *except* a handler thread: stop() blocks
      * until every worker (including the calling one) drains, so a
      * call from inside a handler self-joins and deadlocks (or, on
      * some libmicrohttpd versions, aborts with "Failed to join a
      * thread."). This is the documented DR-008 contract — see
      * specs/architecture/11-decisions/DR-008.md and §5.1.
      *
      * For the same reason, ~webserver() (which calls stop())
      * deadlocks if it runs on a handler thread; destroy the
      * webserver from the thread that constructed it.
      *
      * @return true if the daemon was running and is now stopped;
      *         false if it was already stopped.
     **/
     bool stop();
     /**
      * Method used to evaluate if the server is running or not.
      * @return true if the webserver is running
     **/
     bool is_running();
     // Registration, on_* shortcuts, route(), and unregister_* live in
     // a sibling header to keep this class under the project per-file
     // LOC ceiling. The inner gate forces the header to be included
     // only from within this class body.
#define SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_
#include "httpserver/webserver_routes.hpp"
#undef SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_

     /**
      * Add @p ip (or a range, e.g. "127.0.0.*") to the IP block list.
      * Connections from a matching address are refused at the policy
      * callback. Intended for use under the default ACCEPT policy.
      * No-op semantics are preserved when the same IP is added twice;
      * a more specific entry replaces a previously-recorded wildcard.
      *
      * @param ip an IP literal or wildcard pattern.
      * @see unblock_ip
     **/
     void block_ip(std::string_view ip);

     /**
      * Remove @p ip from the IP block list. Idempotent: removing an IP
      * that is not currently blocked is a no-op.
      *
      * @param ip an IP literal or wildcard pattern previously passed to @ref block_ip.
      * @see block_ip
     **/
     void unblock_ip(std::string_view ip);

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
      * Stop the webserver and wait for in-flight handlers to complete
      * before returning. Use stop() when no such guarantee is required.
     **/
     void stop_and_wait();

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

     /**
      * Reports build-time feature availability (PRD-FLG-REQ-003).
      *
      * The four boolean fields of the returned struct reflect the
      * HAVE_BAUTH / HAVE_DAUTH / HAVE_GNUTLS / HAVE_WEBSOCKET macros at
      * the time libhttpserver was compiled. Use this at runtime to
      * decide whether to register a feature-dependent handler or to
      * surface the configuration to the operator.
      *
      * The values are determined at library build time (not consumer
      * build time): linking against a TLS-disabled libhttpserver always
      * reports `tls == false`, even when the consumer TU was compiled
      * with HAVE_GNUTLS defined.
      *
      * Safe to call before start() and from any thread; never throws.
      **/
     struct features {
         bool basic_auth;
         bool digest_auth;
         bool tls;
         bool websocket;
     };
     static features features() noexcept;

     // Websocket registration surface and lifecycle hook bus (TASK-045)
     // live in sibling headers to keep this class under the project
     // per-file LOC ceiling.
#define SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_
#include "httpserver/webserver_websocket.hpp"
#include "httpserver/webserver_hooks.hpp"
#undef SRC_HTTPSERVER_WEBSERVER_HPP_INSIDE_CLASS_

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
     // TASK-034: stored unconditionally. webserver(create_webserver const&)
     // throws feature_unavailable when this is true but HAVE_BAUTH is off.
     const bool basic_auth_enabled;
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
     const error_handler not_found_handler;
     const error_handler method_not_allowed_handler;
     const internal_error_handler_t internal_error_handler;
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

     // TASK-025/TASK-026: shared lambda-registration helper. Builds-or-
     // merges a hidden detail::lambda_resource shim at @p path, sets every
     // bit in @p methods on it, and stores @p handler into each of those
     // method slots. All seven public on_* overloads and both public
     // route() overloads forward to this single entry point so the
     // merge-and-conflict logic lives in one place. Validation is
     // atomic: if any requested method already has a slot on the path,
     // no slot is mutated and the call throws -- callers therefore see
     // either a fully-installed registration or no change at all.
     // Throws std::invalid_argument if @p methods is empty, if @p
     // handler is empty, if the path conflicts with single_resource
     // mode, if a class-based resource is already registered at the
     // path, or if a lambda is already registered for any requested
     // (method, path).
     void on_methods_(method_set methods,
                      const std::string& path,
                      std::function<http_response(const http_request&)> handler);

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
#if defined(HTTPSERVER_COMPILATION)
     // TASK-027: test-only hook so unit tests in test/unit/ can poke
     // at the v2 route-table impl (lookup_v2, the three tier maps)
     // without widening the public API. The pattern matches the SBO
     // test access friend used by http_response. Gated on
     // HTTPSERVER_COMPILATION so it never appears in installed headers.
     friend struct webserver_test_access;
#endif
};

#if defined(HTTPSERVER_COMPILATION)
// Forward-declared friend giving test code (which compiles with
// HTTPSERVER_COMPILATION via test/Makefile.am AM_CPPFLAGS) a thin
// pointer to the otherwise-private impl_. Defined inline so any TU
// including this header in COMPILATION mode can use it.
struct webserver_test_access {
    static detail::webserver_impl* impl(webserver& w) noexcept {
        return w.impl_.get();
    }
};
#endif

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_WEBSERVER_HPP_
