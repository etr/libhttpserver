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

#ifndef SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_
#define SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_

#include <stdlib.h>
#include <cstdint>
#include <memory>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "httpserver/constants.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

class webserver;
class http_request;

/**
 * Callback signature for the 404 and 405 error-page handlers.
 *
 * Returned by value; the webserver writes the response to the wire.
 * (TASK-030 / PRD-NAM-REQ-003 — see specs/architecture/04-components/create-webserver.md.)
 */
typedef std::function<http_response(const http_request&)> error_handler;

/**
 * Callback signature for @ref create_webserver::internal_error_handler.
 *
 * The handler receives the originating exception's message as a non-owning
 * `std::string_view` — for `std::exception` derivatives this is `e.what()`;
 * for non-`std::exception` throws (`throw 42`) it is the literal string
 * `"unknown exception"`. The view is valid only for the duration of the
 * call; copy if you need to retain it.
 *
 * Full contract: DR-009 §5.2 (see @ref webserver class-level block).
 */
typedef std::function<http_response(const http_request&, std::string_view message)> internal_error_handler_t;

typedef std::function<bool(const std::string&)> validator_ptr;
typedef std::function<void(const std::string&)> log_access_ptr;
typedef std::function<void(const std::string&)> log_error_ptr;
typedef std::function<std::string(const std::string&)> psk_cred_handler_callback;

// SNI (Server Name Indication) callback — see create-webserver.md.
typedef std::function<std::pair<std::string, std::string>(const std::string& server_name)> sni_callback_t;

namespace http { class file_info; }

typedef std::function<bool(const std::string&, const std::string&, const http::file_info&)> file_cleanup_callback_ptr;
typedef std::function<std::shared_ptr<http_response>(const http_request&)> auth_handler_ptr;

/**
 * Fluent builder for @ref webserver instances (PRD-NAM-REQ-004,
 * PRD-CFG-REQ-001..003 / TASK-033).
 *
 * Each setter returns `*this` so calls can chain:
 * `webserver ws{create_webserver{}.port(8080).use_ssl(true).max_threads(4)};`
 *
 * Setters validate eagerly where the input domain is well-defined
 * (e.g. @ref port rejects values outside `[0, 65535]`, all `int` setters
 * reject negatives); feature-gated settings (@ref use_ssl,
 * @ref basic_auth, @ref digest_auth, websocket registration) are
 * validated when the @ref webserver constructor consumes the builder,
 * not at the setter — so a builder configured for an unsupported
 * feature throws @ref feature_unavailable from `webserver(create_webserver)`
 * rather than from the setter.
 *
 * The @ref webserver constructor is `explicit`: callers must direct-init
 * (`webserver ws{cw};`) rather than rely on implicit conversion.
 */
class create_webserver {
 public:
     create_webserver() = default;
     create_webserver(const create_webserver& b) = default;
     create_webserver(create_webserver&& b) noexcept = default;
     create_webserver& operator=(const create_webserver& b) = default;
     create_webserver& operator=(create_webserver&& b) = default;

     explicit create_webserver(uint16_t port): _port(port) { }

     // TASK-033 / PRD-CFG-REQ-003: validate at the setter. The `int` overload
     // exists so out-of-uint16_t values like 70000 are expressible (and
     // throwable); the `uint16_t` overload preserves type-safe calls.
     create_webserver& port(uint16_t port) { _port = port; return *this; }
     create_webserver& port(int port) {
         if (port < 0 || port > 65535) throw_invalid("port", port, "[0, 65535]");
         _port = static_cast<uint16_t>(port); return *this;
     }

     create_webserver& start_method(const http::http_utils::start_method_T& v) { _start_method = v; return *this; }
     create_webserver& max_threads(int v) { check_non_negative("max_threads", v); _max_threads = v; return *this; }
     create_webserver& max_connections(int v) { check_non_negative("max_connections", v); _max_connections = v; return *this; }
     create_webserver& memory_limit(int v) { check_non_negative("memory_limit", v); _memory_limit = v; return *this; }
     create_webserver& content_size_limit(size_t v) { _content_size_limit = v; return *this; }
     create_webserver& connection_timeout(int v) { check_non_negative("connection_timeout", v); _connection_timeout = v; return *this; }
     create_webserver& per_IP_connection_limit(int v) { check_non_negative("per_IP_connection_limit", v); _per_IP_connection_limit = v; return *this; }
     create_webserver& log_access(log_access_ptr v) { _log_access = v; return *this; }
     create_webserver& log_error(log_error_ptr v) { _log_error = v; return *this; }
     create_webserver& validator(validator_ptr v) { _validator = v; return *this; }
     create_webserver& unescaper(unescaper_ptr v) { _unescaper = v; return *this; }
     create_webserver& bind_address(const struct sockaddr* v) { _bind_address = v; return *this; }
     create_webserver& bind_address(const std::string& ip);
     create_webserver& bind_socket(int v) { _bind_socket = v; return *this; }
     create_webserver& max_thread_stack_size(int v) { check_non_negative("max_thread_stack_size", v); _max_thread_stack_size = v; return *this; }

     // Boolean flag setters (TASK-033 / PRD-CFG-REQ-001).
     /**
      * Enable TLS for the webserver (HTTPS).
      *
      * On a `HAVE_GNUTLS`-off build, constructing a @ref webserver from a
      * builder with `use_ssl(true)` throws @ref feature_unavailable.
      * Defaults to `false`.
      *
      * @param enable `true` to enable TLS, `false` to disable.
      * @return reference to this builder for chaining.
      */
     create_webserver& use_ssl(bool enable = true) { _use_ssl = enable; return *this; }
     create_webserver& use_ipv6(bool enable = true) { _use_ipv6 = enable; return *this; }
     create_webserver& use_dual_stack(bool enable = true) { _use_dual_stack = enable; return *this; }
     create_webserver& debug(bool enable = true) { _debug = enable; return *this; }
     create_webserver& pedantic(bool enable = true) { _pedantic = enable; return *this; }

     create_webserver& https_mem_key(const std::string& v) { _https_mem_key = http::load_file(v); return *this; }
     create_webserver& https_mem_cert(const std::string& v) { _https_mem_cert = http::load_file(v); return *this; }
     create_webserver& https_mem_trust(const std::string& v) { _https_mem_trust = http::load_file(v); return *this; }
     create_webserver& raw_https_mem_key(const std::string& v) { _https_mem_key = v; return *this; }
     create_webserver& raw_https_mem_cert(const std::string& v) { _https_mem_cert = v; return *this; }
     create_webserver& raw_https_mem_trust(const std::string& v) { _https_mem_trust = v; return *this; }
     create_webserver& https_priorities(const std::string& v) { _https_priorities = v; return *this; }
     create_webserver& cred_type(const http::http_utils::cred_type_T& v) { _cred_type = v; return *this; }
     create_webserver& psk_cred_handler(psk_cred_handler_callback v) { _psk_cred_handler = v; return *this; }
     create_webserver& digest_auth_random(const std::string& v) { _digest_auth_random = v; return *this; }
     create_webserver& nonce_nc_size(int v) { check_non_negative("nonce_nc_size", v); _nonce_nc_size = v; return *this; }
     create_webserver& default_policy(const http::http_utils::policy_T& v) { _default_policy = v; return *this; }

     // TASK-034 / PRD-FLG-REQ-001: setter is unconditional. The actual
     // validation lives in webserver(const create_webserver&), which
     // throws feature_unavailable when this is set to true on a
     // HAVE_BAUTH-off build.
     /**
      * Enable HTTP Basic authentication on the webserver.
      *
      * On a `HAVE_BAUTH`-off build, constructing a @ref webserver from a
      * builder with `basic_auth(true)` throws @ref feature_unavailable.
      * Default value depends on the build flag (see TASK-034).
      *
      * @param enable `true` to enable Basic auth, `false` to disable.
      * @return reference to this builder for chaining.
      */
     create_webserver& basic_auth(bool enable = true) { _basic_auth_enabled = enable; return *this; }
     /**
      * Enable HTTP Digest authentication on the webserver.
      *
      * On a `HAVE_DAUTH`-off build, constructing a @ref webserver from a
      * builder with `digest_auth(true)` throws @ref feature_unavailable.
      * Default value depends on the build flag.
      *
      * @param enable `true` to enable Digest auth, `false` to disable.
      * @return reference to this builder for chaining.
      */
     create_webserver& digest_auth(bool enable = true) { _digest_auth_enabled = enable; return *this; }
     create_webserver& deferred(bool enable = true) { _deferred_enabled = enable; return *this; }
     create_webserver& regex_checking(bool enable = true) { _regex_checking = enable; return *this; }
     create_webserver& ban_system(bool enable = true) { _ban_system_enabled = enable; return *this; }
     create_webserver& post_process(bool enable = true) { _post_process_enabled = enable; return *this; }
     create_webserver& put_processed_data_to_content(bool enable = true) { _put_processed_data_to_content = enable; return *this; }

     create_webserver& file_upload_target(const file_upload_target_T& v) { _file_upload_target = v; return *this; }
     create_webserver& file_upload_dir(const std::string& v) {
         if (v.empty()) throw std::invalid_argument("file_upload_dir: must not be empty");
         _file_upload_dir = v; return *this;
     }
     create_webserver& generate_random_filename_on_upload(bool enable = true) { _generate_random_filename_on_upload = enable; return *this; }
     create_webserver& single_resource(bool enable = true) { _single_resource = enable; return *this; }
     create_webserver& tcp_nodelay(bool enable = true) { _tcp_nodelay = enable; return *this; }
     /**
      * Install a handler invoked when no resource matches the request path (HTTP 404).
      *
      * The handler returns an @ref http_response by value; its status
      * code, headers, and body are sent on the wire as-is. If null, a
      * default 404 response is generated.
      *
      * @note This is an alias. Calling it (with a non-null callable)
      *       installs a hook at @ref httpserver::hook_phase::route_resolved.
      *       Equivalent to `ws.add_hook(hook_phase::route_resolved, ...)`
      *       at webserver construction (TASK-048 / PRD-HOOK-REQ-009 / §4.10).
      *       The on-the-wire 404 synthesis flows through the v1 dispatch
      *       path; the hook registration is the alias-equivalence story.
      *
      * @param h error_handler callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      * @see method_not_allowed_handler, internal_error_handler
      */
     create_webserver& not_found_handler(error_handler h) { _not_found_handler = std::move(h); return *this; }
     /**
      * Install a handler invoked when a resource matches the path but
      * not the HTTP method (HTTP 405).
      *
      * The handler returns an @ref http_response by value. If null, a
      * default 405 response is generated.
      *
      * @note This is an alias. Calling it (with a non-null callable)
      *       installs a hook at @ref httpserver::hook_phase::before_handler.
      *       Equivalent to `ws.add_hook(hook_phase::before_handler, ...)`
      *       at webserver construction (TASK-048 / PRD-HOOK-REQ-009 / §4.10).
      *       The on-the-wire 405 synthesis flows through the v1 dispatch
      *       path; the hook registration is the alias-equivalence story.
      *
      * @param h error_handler callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      * @see not_found_handler, internal_error_handler
      */
     create_webserver& method_not_allowed_handler(error_handler h) { _method_not_allowed_handler = std::move(h); return *this; }
     /**
      * Install the handler invoked when a registered request handler
      * throws (HTTP 500 by default).
      *
      * This is the load-bearing extension point for the dispatch
      * error-propagation contract (DR-009 §5.2 / PRD-FLG-REQ-002). The
      * contract — full statement on the @ref webserver class block — is:
      *
      *   1. The handler call is wrapped in `try/catch (std::exception&) / catch (...)`.
      *   2. On `std::exception`: the message is logged via @ref log_error,
      *      then @p h is invoked with `e.what()` as the @ref internal_error_handler_t
      *      `message` argument.
      *   3. On non-`std::exception`: same path with the message replaced
      *      by the literal string `"unknown exception"`.
      *   4. If @p h itself throws while servicing the above, the failure
      *      is logged generically and a hardcoded 500 with an EMPTY body
      *      is sent. No exception ever escapes into libmicrohttpd.
      *   5. @ref feature_unavailable (a `std::runtime_error` subclass) is
      *      NOT mapped specially: it lands as a generic 500 like any
      *      other `std::exception`.
      *   6. May run concurrently from multiple MHD worker threads;
      *      implementations MUST be thread-safe.
      *
      * If @p h is null, the default response is a 500 with the message
      * in the body (see DR-009).
      *
      * @param h @ref internal_error_handler_t callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      * @see webserver, not_found_handler, method_not_allowed_handler, feature_unavailable
      */
     create_webserver& internal_error_handler(internal_error_handler_t h) { _internal_error_handler = std::move(h); return *this; }
     create_webserver& file_cleanup_callback(file_cleanup_callback_ptr v) { _file_cleanup_callback = v; return *this; }
     /**
      * Install the centralised auth handler invoked before every
      * dispatched request whose path is not on the @ref auth_skip_paths
      * list. Returning a non-null `shared_ptr<http_response>` short-
      * circuits dispatch and sends that response on the wire
      * (typically a 401 or 403).
      *
      * @note This is an alias. Calling it (with a non-null callable)
      *       installs a hook at @ref httpserver::hook_phase::before_handler.
      *       Equivalent to `ws.add_hook(hook_phase::before_handler, ...)`
      *       at webserver construction (TASK-048 / PRD-HOOK-REQ-009 / §4.10).
      *       The on-the-wire auth short-circuit flows through the v1
      *       dispatch path; the hook registration is the
      *       alias-equivalence story.
      *
      * @param v auth_handler_ptr callback; pass `nullptr` to clear.
      * @return reference to this builder for chaining.
      */
     create_webserver& auth_handler(auth_handler_ptr v) { _auth_handler = v; return *this; }
     create_webserver& auth_skip_paths(const std::vector<std::string>& v) { _auth_skip_paths = v; return *this; }
     create_webserver& sni_callback(sni_callback_t v) { _sni_callback = v; return *this; }

     // TASK-033: renamed from no_listen_socket()/no_thread_safety()/no_alpn();
     // public-API polarity is inverted (private field still stores the "no_"
     // form to avoid churning webserver.cpp).
     create_webserver& listen_socket(bool enable = true) { _no_listen_socket = !enable; return *this; }
     create_webserver& thread_safety(bool enable = true) { _no_thread_safety = !enable; return *this; }
     create_webserver& alpn(bool enable = true) { _no_alpn = !enable; return *this; }

     create_webserver& turbo(bool enable = true) { _turbo = enable; return *this; }
     create_webserver& suppress_date_header(bool enable = true) { _suppress_date_header = enable; return *this; }
     create_webserver& listen_backlog(int v) { check_non_negative("listen_backlog", v); _listen_backlog = v; return *this; }
     create_webserver& address_reuse(int v) { check_non_negative("address_reuse", v); _address_reuse = v; return *this; }
     create_webserver& connection_memory_increment(size_t v) { _connection_memory_increment = v; return *this; }
     create_webserver& tcp_fastopen_queue_size(int v) { check_non_negative("tcp_fastopen_queue_size", v); _tcp_fastopen_queue_size = v; return *this; }
     create_webserver& sigpipe_handled_by_app(bool enable = true) { _sigpipe_handled_by_app = enable; return *this; }
     create_webserver& https_mem_dhparams(const std::string& v) { _https_mem_dhparams = v; return *this; }
     create_webserver& https_key_password(const std::string& v) { _https_key_password = v; return *this; }
     create_webserver& https_priorities_append(const std::string& v) { _https_priorities_append = v; return *this; }
     create_webserver& client_discipline_level(int v) {
         if (v < -1) throw_invalid("client_discipline_level", v, ">= -1");
         _client_discipline_level = v; return *this;
     }

 private:
     // Throw helpers (TASK-033 / PRD-CFG-REQ-003). Defined inline so the
     // single-line setters above stay one-liners.
     static void throw_invalid(const char* name, int64_t value, const char* range) {
         throw std::invalid_argument(std::string(name) + ": " + std::to_string(value) + " out of range " + range);
     }
     static void check_non_negative(const char* name, int v) {
         if (v < 0) throw std::invalid_argument(std::string(name) + ": " + std::to_string(v) + " must be >= 0");
     }

     uint16_t _port = constants::DEFAULT_WS_PORT;
     http::http_utils::start_method_T _start_method = http::http_utils::INTERNAL_SELECT;
     int _max_threads = 0;
     int _max_connections = 0;
     int _memory_limit = 0;
     size_t _content_size_limit = std::numeric_limits<size_t>::max();
     int _connection_timeout = constants::DEFAULT_WS_TIMEOUT;
     int _per_IP_connection_limit = 0;
     log_access_ptr _log_access = nullptr;
     log_error_ptr _log_error = nullptr;
     validator_ptr _validator = nullptr;
     unescaper_ptr _unescaper = nullptr;
     const struct sockaddr* _bind_address = nullptr;
     std::shared_ptr<struct sockaddr_storage> _bind_address_storage;
     int _bind_socket = 0;
     int _max_thread_stack_size = 0;
     bool _use_ssl = false;
     bool _use_ipv6 = false;
     bool _use_dual_stack = false;
     bool _debug = false;
     bool _pedantic = false;
     std::string _https_mem_key = "";
     std::string _https_mem_cert = "";
     std::string _https_mem_trust = "";
     std::string _https_priorities = "";
     http::http_utils::cred_type_T _cred_type = http::http_utils::NONE;
     psk_cred_handler_callback _psk_cred_handler = nullptr;
     std::string _digest_auth_random = "";
     int _nonce_nc_size = 0;
     http::http_utils::policy_T _default_policy = http::http_utils::ACCEPT;
     // TASK-034: stored unconditionally. The default values are computed
     // by basic_auth_default() and digest_auth_default() in
     // create_webserver.cpp, where the HAVE_BAUTH / HAVE_DAUTH build
     // flags are reachable — that keeps the public header free of
     // build-flag preprocessor gates (PRD-FLG-REQ-001) while preserving
     // the historical defaults (true on the respective auth-on builds;
     // false on auth-off builds so an unmodified builder doesn't trip
     // the feature_unavailable throw at construction time).
     static bool basic_auth_default() noexcept;
     static bool digest_auth_default() noexcept;
     bool _basic_auth_enabled = basic_auth_default();
     bool _digest_auth_enabled = digest_auth_default();
     bool _regex_checking = true;
     bool _ban_system_enabled = true;
     bool _post_process_enabled = true;
     bool _put_processed_data_to_content = true;
     file_upload_target_T _file_upload_target = FILE_UPLOAD_MEMORY_ONLY;
     std::string _file_upload_dir = "/tmp";
     bool _generate_random_filename_on_upload = false;
     bool _deferred_enabled = false;
     bool _single_resource = false;
     bool _tcp_nodelay = false;
     error_handler _not_found_handler = nullptr;
     error_handler _method_not_allowed_handler = nullptr;
     internal_error_handler_t _internal_error_handler = nullptr;
     file_cleanup_callback_ptr _file_cleanup_callback = nullptr;
     auth_handler_ptr _auth_handler = nullptr;
     std::vector<std::string> _auth_skip_paths;
     sni_callback_t _sni_callback = nullptr;
     bool _no_listen_socket = false;
     bool _no_thread_safety = false;
     bool _turbo = false;
     bool _suppress_date_header = false;
     int _listen_backlog = 0;
     int _address_reuse = 0;
     size_t _connection_memory_increment = 0;
     int _tcp_fastopen_queue_size = 0;
     bool _sigpipe_handled_by_app = false;
     std::string _https_mem_dhparams = "";
     std::string _https_key_password = "";
     std::string _https_priorities_append = "";
     bool _no_alpn = false;
     int _client_discipline_level = -1;

     friend class webserver;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_
