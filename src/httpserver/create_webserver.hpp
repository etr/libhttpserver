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
#include <optional>
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
 * `"unknown exception"`.
 *
 * @note The `message` view is valid only for the duration of the call.
 *       Copy it into a `std::string` if you need to retain it beyond
 *       the handler's return.
 *
 * @security The `message` parameter originates from application exception
 * text and MAY contain internal detail (DB connection strings, file paths,
 * or attacker-influenced data that triggered the exception). Implementations
 * MUST NOT forward this value into HTTP response bodies without sanitization
 * (CWE-209: Information Exposure Through an Error Message). See also
 * @ref handler_exception_ctx::message. (Finding #32 / security-reviewer.)
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

/**
 * Centralised authentication callback signature.
 *
 * Returning `std::nullopt` allows the request to proceed to dispatch.
 * Returning an engaged `std::optional<http_response>` short-circuits the
 * before_handler chain and sends that response on the wire (typically a
 * 401 produced by `http_response::unauthorized(realm)`).
 *
 * Migrated from `std::function<std::shared_ptr<http_response>(...)>` to
 * remove the per-authenticated-request control-block allocation; the
 * by-value `http_response` carries small bodies via SBO without any heap
 * traffic. See TASK-054, DR-009, PRD-RSP-REQ-007.
 */
typedef std::function<std::optional<http_response>(const http_request&)> auth_handler_ptr;

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

     explicit create_webserver(std::uint16_t port): _port(port) { }

     // TASK-033 / PRD-CFG-REQ-003: validate at the setter. The `int` overload
     // exists so out-of-uint16_t values like 70000 are expressible (and
     // throwable); the `std::uint16_t` overload preserves type-safe calls.
     create_webserver& port(std::uint16_t port) { _port = port; return *this; }
     create_webserver& port(int port) {
         if (port < 0 || port > 65535) throw_invalid("port", port, "[0, 65535]");
         _port = static_cast<std::uint16_t>(port); return *this;
     }

     create_webserver& start_method(const http::http_utils::start_method_T& v) { _start_method = v; return *this; }
     create_webserver& max_threads(int v) { check_non_negative("max_threads", v); _max_threads = v; return *this; }
     create_webserver& max_connections(int v) { check_non_negative("max_connections", v); _max_connections = v; return *this; }
     create_webserver& memory_limit(int v) { check_non_negative("memory_limit", v); _memory_limit = v; return *this; }
     create_webserver& content_size_limit(size_t v) { _content_size_limit = v; return *this; }
     create_webserver& connection_timeout(int v) { check_non_negative("connection_timeout", v); _connection_timeout = v; return *this; }
     create_webserver& per_IP_connection_limit(int v) { check_non_negative("per_IP_connection_limit", v); _per_IP_connection_limit = v; return *this; }
     /**
      * Install a legacy access-log callback invoked after every response
      * is sent (HTTP 2xx, 4xx, 5xx — any status).
      *
      * The callback receives a single pre-formatted string:
      * `<path> METHOD: <method>`. Control characters in path and method
      * are replaced with '-' to prevent log-injection (CWE-117).
      *
      * @note This is an alias. Calling it (with a non-null callable)
      *       installs a hook at @ref httpserver::hook_phase::response_sent
      *       via a dedicated alias slot on the webserver implementation
      *       (TASK-050 / PRD-HOOK-REQ-009 / §4.10). The alias fires AFTER
      *       any user-added response_sent hooks so those hooks observe the
      *       response first. Re-registration replaces the previous callable.
      *       For richer structured access logging — including HTTP status
      *       code, byte count, and request duration — prefer
      *       `ws.add_hook(hook_phase::response_sent, ...)` directly, which
      *       provides the full @ref httpserver::response_sent_ctx (the data
      *       issues #281 and #69 asked for). See `examples/clf_access_log.cpp`
      *       for a Common Log Format example.
      *
      * @param v log_access_ptr callback; pass `nullptr` to clear.
      *        The callable is stored in a `std::function` and MUST be
      *        CopyConstructible. Move-only callables are not accepted.
      * @return reference to this builder for chaining.
      * @see webserver, not_found_handler, internal_error_handler
      */
     create_webserver& log_access(log_access_ptr v) { _log_access = std::move(v); return *this; }
     create_webserver& log_error(log_error_ptr v) { _log_error = std::move(v); return *this; }
     [[deprecated("validator callback is not invoked by v2 dispatch; use webserver::add_hook(hook_phase::request_received, ...) instead")]]
     create_webserver& validator(validator_ptr v) { _validator = std::move(v); return *this; }
     create_webserver& unescaper(unescaper_ptr v) { _unescaper = v; return *this; }
     create_webserver& bind_address(const struct sockaddr* v) { _bind_address = v; return *this; }
     create_webserver& bind_address(const std::string& ip);
     create_webserver& bind_socket(int v) { _bind_socket = v; return *this; }
     create_webserver& max_thread_stack_size(int v) { check_non_negative("max_thread_stack_size", v); _max_thread_stack_size = v; return *this; }

     /**
      * Per-request hard limit on the number of distinct GET argument keys.
      *
      * Caps how many unique `?key=value` pairs `populate_args()` will accept
      * before returning MHD_NO. The guard mitigates arena exhaustion from
      * crafted requests with thousands of unique parameters
      * (security-reviewer-iter1-2). The same protection on a byte basis is
      * provided by @ref max_args_bytes.
      *
      * Pass `0` to keep the compile-time default
      * (@ref httpserver::detail::arguments_accumulator::DEFAULT_MAX_ARGS_COUNT,
      * currently 64). POST argument limits are unaffected — those are bounded
      * upstream by MHD_OPTION_CONNECTION_MEMORY_LIMIT
      * (see @ref memory_limit / @ref content_size_limit).
      */
     create_webserver& max_args_count(std::size_t v) { _max_args_count = v; return *this; }

     /**
      * Per-request hard limit on the total bytes (sum of all key + value
      * lengths) of GET arguments accepted before `populate_args()` returns
      * MHD_NO. Complements @ref max_args_count; both guards must pass.
      *
      * Pass `0` to keep the compile-time default
      * (@ref httpserver::detail::arguments_accumulator::DEFAULT_MAX_ARGS_BYTES,
      * currently 64 KiB).
      */
     create_webserver& max_args_bytes(std::size_t v) { _max_args_bytes = v; return *this; }

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
     // Security note (CWE-11): debug(true) routes verbose libmicrohttpd
     // internal messages (connection details, MHD state) through the
     // log_error callback. Must not be set in production builds. Guard
     // with `#ifndef NDEBUG` or an explicit environment check.
     create_webserver& debug(bool enable = true) { _debug = enable; return *this; }
     create_webserver& pedantic(bool enable = true) { _pedantic = enable; return *this; }
     /**
      * Restore the v1 / pre-DR-009-Revision-1 behaviour of surfacing the
      * originating exception message in the default internal-server-error
      * response body.
      *
      * @warning CWE-209: exception messages routinely contain file paths,
      *          SQL fragments, internal identifiers, and other detail that
      *          should not cross a process boundary to an untrusted client.
      *          Enable only in development or behind an explicit
      *          `#ifndef NDEBUG` guard.
      *
      * Default is `false`: the default body is the fixed string
      * `"Internal Server Error"`. The `log_error` callback continues to
      * receive the originating message regardless of this flag; only the
      * HTTP response body is affected.
      *
      * @param enable `true` to expose exception messages in the default
      *               500 body (dev only), `false` for the sanitized
      *               fixed-string default.
      * @return reference to this builder for chaining.
      * @see DR-009 Revision 1 (specs/architecture/11-decisions/DR-009.md)
      */
     create_webserver& expose_exception_messages(bool enable = true) {
         _expose_exception_messages = enable; return *this;
     }

     /**
      * Restore the v1 / pre-TASK-057 behaviour of streaming credential
      * material verbatim from @ref httpserver::operator<<(std::ostream&, const http_request&).
      *
      * @warning CWE-312 / CWE-532 (OWASP A09:2021): when this flag is
      *          enabled and the dump is routed to a log aggregator
      *          (`log_access`, `log_error`, stdout-capturing systemd, or
      *          a centralised syslog / SIEM pipeline), every Basic-auth
      *          password, every Authorization / Proxy-Authorization
      *          header value, and every cookie / session token is
      *          written in plaintext to the log store. Enable only in
      *          development or behind an explicit `#ifndef NDEBUG`
      *          guard.
      *
      * Default is `false`: `pass`, `Authorization`, `Proxy-Authorization`,
      * and every cookie value are streamed as the fixed token
      * `"<redacted>"`. The username (`user:"..."`) is never redacted —
      * it is an identifier, not a secret (REMOTE_USER access-log
      * convention).
      *
      * @param enable `true` to expose credentials in diagnostic dumps
      *               (dev only), `false` for the default redaction.
      * @return reference to this builder for chaining.
      */
     create_webserver& expose_credentials_in_logs(bool enable = true) {
         _expose_credentials_in_logs = enable; return *this;
     }

     create_webserver& https_mem_key(const std::string& v) { _https_mem_key = http::load_file(v); return *this; }
     create_webserver& https_mem_cert(const std::string& v) { _https_mem_cert = http::load_file(v); return *this; }
     create_webserver& https_mem_trust(const std::string& v) { _https_mem_trust = http::load_file(v); return *this; }
     create_webserver& raw_https_mem_key(const std::string& v) { _https_mem_key = v; return *this; }
     create_webserver& raw_https_mem_cert(const std::string& v) { _https_mem_cert = v; return *this; }
     create_webserver& raw_https_mem_trust(const std::string& v) { _https_mem_trust = v; return *this; }
     create_webserver& https_priorities(std::string v) { _https_priorities = std::move(v); return *this; }
     create_webserver& cred_type(const http::http_utils::cred_type_T& v) { _cred_type = v; return *this; }
     create_webserver& psk_cred_handler(psk_cred_handler_callback v) { _psk_cred_handler = std::move(v); return *this; }
     create_webserver& digest_auth_random(std::string v) { _digest_auth_random = std::move(v); return *this; }
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
      * Default: `true` when `HAVE_BAUTH` is defined (auth-on build),
      * `false` otherwise.
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
      * Default: `true` when `HAVE_DAUTH` is defined (auth-on build),
      * `false` otherwise.
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
     /**
      * Set the directory where uploaded files are written to disk.
      *
      * @warning The default upload directory is `/tmp`, which is world-
      *          readable on most POSIX systems. Uploaded files with
      *          predictable names can be read by any local user until
      *          the request destructor removes them. Set this to a
      *          private directory AND enable
      *          `generate_random_filename_on_upload(true)` whenever
      *          uploaded files may contain sensitive content.
      *
      * @param v path to the upload staging directory; must not be empty.
      * @return reference to this builder for chaining.
      * @see generate_random_filename_on_upload
      */
     create_webserver& file_upload_dir(const std::string& v) {
         if (v.empty()) throw std::invalid_argument("file_upload_dir: must not be empty");
         _file_upload_dir = v; return *this;
     }
     /**
      * When enabled, uploaded files are stored under a randomly-generated
      * filename rather than the client-supplied filename.
      *
      * @note Combine with a non-world-readable `file_upload_dir` to
      *       reduce the window in which uploaded sensitive content is
      *       accessible to other local users.
      *
      * @param enable `true` to randomise filenames, `false` to use the
      *               client-supplied name.
      * @return reference to this builder for chaining.
      * @see file_upload_dir
      */
     create_webserver& generate_random_filename_on_upload(bool enable = true) { _generate_random_filename_on_upload = enable; return *this; }
     create_webserver& single_resource(bool enable = true) { _single_resource = enable; return *this; }
     create_webserver& tcp_nodelay(bool enable = true) { _tcp_nodelay = enable; return *this; }
     // The handler/callback setters and the remaining TLS / connection-
     // tuning setters live in a sibling header to keep this class
     // definition under the project per-file LOC ceiling. The inner gate
     // forces the header to be included only from within this class body.
#define SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_INSIDE_CLASS_
#include "httpserver/create_webserver_setters.hpp"
#undef SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_INSIDE_CLASS_

 private:
     // Throw helpers (TASK-033 / PRD-CFG-REQ-003). Defined inline so the
     // single-line setters above stay one-liners.
     static void throw_invalid(const char* name, int64_t value, const char* range) {
         throw std::invalid_argument(std::string(name) + ": " + std::to_string(value) + " out of range " + range);
     }
     static void check_non_negative(const char* name, int v) {
         if (v < 0) throw std::invalid_argument(std::string(name) + ": " + std::to_string(v) + " must be >= 0");
     }

     std::uint16_t _port = constants::DEFAULT_WS_PORT;
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
     // 0 = use the compile-time defaults
     // (arguments_accumulator::DEFAULT_MAX_ARGS_COUNT / _BYTES).
     std::size_t _max_args_count = 0;
     std::size_t _max_args_bytes = 0;
     bool _use_ssl = false;
     bool _use_ipv6 = false;
     bool _use_dual_stack = false;
     bool _debug = false;
     bool _pedantic = false;
     // TASK-055 / DR-009 Revision 1: default false (CWE-209 fix). When
     // true, internal_error_page surfaces the originating exception's
     // message in the default 500 body (development-only behaviour).
     bool _expose_exception_messages = false;
     // TASK-057: default false (CWE-312 / CWE-532 fix). When true,
     // http_request::operator<< restores the v1 verbose form for the
     // four credential surfaces (pass, Authorization /
     // Proxy-Authorization header values, cookie values).
     bool _expose_credentials_in_logs = false;
     std::string _https_mem_key;
     std::string _https_mem_cert;
     std::string _https_mem_trust;
     std::string _https_priorities;
     http::http_utils::cred_type_T _cred_type = http::http_utils::NONE;
     psk_cred_handler_callback _psk_cred_handler = nullptr;
     std::string _digest_auth_random;
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
     std::string _https_mem_dhparams;
     std::string _https_key_password;
     std::string _https_priorities_append;
     bool _no_alpn = false;
     int _client_discipline_level = -1;

     friend class webserver;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_
