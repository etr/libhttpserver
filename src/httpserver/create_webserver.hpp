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
 * @ref handler_exception_ctx::message.
 *
 * Full contract: see the @ref webserver class-level block.
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
 * The by-value `http_response` return avoids a per-authenticated-request
 * shared_ptr control-block allocation; small bodies travel via SBO
 * without any heap traffic.
 */
typedef std::function<std::optional<http_response>(const http_request&)> auth_handler_ptr;

namespace detail {
// Historical auth defaults. Defined in create_webserver.cpp, where the
// HAVE_BAUTH / HAVE_DAUTH build flags are reachable -- this keeps the
// public header free of build-flag preprocessor gates while preserving
// the historical defaults (true on the respective auth-on builds; false
// on auth-off builds, so an unmodified builder does not trip the
// feature_unavailable throw at construction time).
bool default_basic_auth_enabled() noexcept;
bool default_digest_auth_enabled() noexcept;
}  // namespace detail

/**
 * The full set of webserver configuration inputs, in one struct.
 *
 * A single @ref create_webserver builder owns one of these and its setters
 * mutate it; the @ref webserver constructor copies it wholesale into its
 * own `const webserver_config`. Storing the config as one struct (rather
 * than ~65 parallel members duplicated across the builder and the server)
 * means adding a new option touches one field here plus one builder setter,
 * instead of four-plus separate sites.
 *
 * Every field is a builder input. Values derived at construction time (e.g.
 * the normalized auth-skip path list) are NOT here -- they live on the
 * @ref webserver directly.
 */
struct webserver_config {
    std::uint16_t port = constants::DEFAULT_WS_PORT;
    http::http_utils::start_method_T start_method = http::http_utils::INTERNAL_SELECT;
    int max_threads = 0;
    int max_connections = 0;
    int memory_limit = 0;
    size_t content_size_limit = std::numeric_limits<size_t>::max();
    int connection_timeout = constants::DEFAULT_WS_TIMEOUT;
    int per_IP_connection_limit = 0;
    log_access_ptr log_access = nullptr;
    log_error_ptr log_error = nullptr;
    validator_ptr validator = nullptr;
    unescaper_ptr unescaper = nullptr;
    const struct sockaddr* bind_address = nullptr;
    std::shared_ptr<struct sockaddr_storage> bind_address_storage;
    int bind_socket = 0;
    int max_thread_stack_size = 0;
    // 0 = use the compile-time defaults
    // (arguments_accumulator::DEFAULT_MAX_ARGS_COUNT / _BYTES).
    std::size_t max_args_count = 0;
    std::size_t max_args_bytes = 0;
    bool use_ssl = false;
    bool use_ipv6 = false;
    bool use_dual_stack = false;
    bool debug = false;
    bool pedantic = false;
    // Default false (CWE-209 fix). When true, internal_error_page surfaces
    // the originating exception's message in the default 500 body
    // (development-only behaviour).
    bool expose_exception_messages = false;
    // Default false (CWE-312 / CWE-532 fix). When true, http_request's
    // operator<< restores the v1 verbose form for the four credential
    // surfaces (pass, Authorization / Proxy-Authorization header values,
    // cookie values).
    bool expose_credentials_in_logs = false;
    std::string https_mem_key;
    std::string https_mem_cert;
    std::string https_mem_trust;
    std::string https_priorities;
    http::http_utils::cred_type_T cred_type = http::http_utils::NONE;
    psk_cred_handler_callback psk_cred_handler = nullptr;
    std::string digest_auth_random;
    int nonce_nc_size = 0;
    http::http_utils::policy_T default_policy = http::http_utils::ACCEPT;
    bool basic_auth_enabled = detail::default_basic_auth_enabled();
    bool digest_auth_enabled = detail::default_digest_auth_enabled();
    bool regex_checking = true;
    bool ip_access_control_enabled = true;
    bool post_process_enabled = true;
    bool put_processed_data_to_content = true;
    file_upload_target_T file_upload_target = FILE_UPLOAD_MEMORY_ONLY;
    std::string file_upload_dir = "/tmp";
    bool generate_random_filename_on_upload = false;
    bool deferred_enabled = false;
    bool single_resource = false;
    bool tcp_nodelay = false;
    error_handler not_found_handler = nullptr;
    error_handler method_not_allowed_handler = nullptr;
    internal_error_handler_t internal_error_handler = nullptr;
    file_cleanup_callback_ptr file_cleanup_callback = nullptr;
    auth_handler_ptr auth_handler = nullptr;
    std::vector<std::string> auth_skip_paths;
    sni_callback_t sni_callback = nullptr;
    bool no_listen_socket = false;
    bool no_thread_safety = false;
    bool turbo = false;
    bool suppress_date_header = false;
    int listen_backlog = 0;
    int address_reuse = 0;
    size_t connection_memory_increment = 0;
    int tcp_fastopen_queue_size = 0;
    bool sigpipe_handled_by_app = false;
    std::string https_mem_dhparams;
    std::string https_key_password;
    std::string https_priorities_append;
    bool no_alpn = false;
    int client_discipline_level = -1;
};

/**
 * Fluent builder for @ref webserver instances.
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

     explicit create_webserver(std::uint16_t port) { _config.port = port; }

     // Validate at the setter. The `int` overload
     // exists so out-of-uint16_t values like 70000 are expressible (and
     // throwable); the `std::uint16_t` overload preserves type-safe calls.
     create_webserver& port(std::uint16_t port) { _config.port = port; return *this; }
     create_webserver& port(int port) {
         if (port < 0 || port > 65535) throw_invalid("port", port, "[0, 65535]");
         _config.port = static_cast<std::uint16_t>(port); return *this;
     }

     create_webserver& start_method(const http::http_utils::start_method_T& v) { _config.start_method = v; return *this; }
     create_webserver& max_threads(int v) { check_non_negative("max_threads", v); _config.max_threads = v; return *this; }
     create_webserver& max_connections(int v) { check_non_negative("max_connections", v); _config.max_connections = v; return *this; }
     create_webserver& memory_limit(int v) { check_non_negative("memory_limit", v); _config.memory_limit = v; return *this; }
     create_webserver& content_size_limit(size_t v) { _config.content_size_limit = v; return *this; }
     create_webserver& connection_timeout(int v) { check_non_negative("connection_timeout", v); _config.connection_timeout = v; return *this; }
     create_webserver& per_IP_connection_limit(int v) { check_non_negative("per_IP_connection_limit", v); _config.per_IP_connection_limit = v; return *this; }
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
      *       via a dedicated alias slot on the webserver implementation.
      *       The alias fires AFTER
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
     create_webserver& log_access(log_access_ptr v) { _config.log_access = std::move(v); return *this; }
     create_webserver& log_error(log_error_ptr v) { _config.log_error = std::move(v); return *this; }
     [[deprecated("validator callback is not invoked by v2 dispatch; use webserver::add_hook(hook_phase::request_received, ...) instead")]]
     create_webserver& validator(validator_ptr v) { _config.validator = std::move(v); return *this; }
     create_webserver& unescaper(unescaper_ptr v) { _config.unescaper = v; return *this; }
     create_webserver& bind_address(const struct sockaddr* v) { _config.bind_address = v; return *this; }
     create_webserver& bind_address(const std::string& ip);
     create_webserver& bind_socket(int v) { _config.bind_socket = v; return *this; }
     create_webserver& max_thread_stack_size(int v) { check_non_negative("max_thread_stack_size", v); _config.max_thread_stack_size = v; return *this; }

     /**
      * Per-request hard limit on the number of distinct GET argument keys.
      *
      * Caps how many unique `?key=value` pairs `populate_args()` will accept
      * before returning MHD_NO. The guard mitigates arena exhaustion from
      * crafted requests with thousands of unique parameters. The same
      * protection on a byte basis is provided by @ref max_args_bytes.
      *
      * Pass `0` to keep the compile-time default
      * (@c httpserver::detail::arguments_accumulator::DEFAULT_MAX_ARGS_COUNT,
      * currently 64). POST argument limits are unaffected — those are bounded
      * upstream by MHD_OPTION_CONNECTION_MEMORY_LIMIT
      * (see @ref memory_limit / @ref content_size_limit).
      */
     create_webserver& max_args_count(std::size_t v) { _config.max_args_count = v; return *this; }

     /**
      * Per-request hard limit on the total bytes (sum of all key + value
      * lengths) of GET arguments accepted before `populate_args()` returns
      * MHD_NO. Complements @ref max_args_count; both guards must pass.
      *
      * Pass `0` to keep the compile-time default
      * (@c httpserver::detail::arguments_accumulator::DEFAULT_MAX_ARGS_BYTES,
      * currently 64 KiB).
      */
     create_webserver& max_args_bytes(std::size_t v) { _config.max_args_bytes = v; return *this; }

     // Boolean flag setters.
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
     create_webserver& use_ssl(bool enable = true) { _config.use_ssl = enable; return *this; }
     create_webserver& use_ipv6(bool enable = true) { _config.use_ipv6 = enable; return *this; }
     create_webserver& use_dual_stack(bool enable = true) { _config.use_dual_stack = enable; return *this; }
     // Security note (CWE-11): debug(true) routes verbose libmicrohttpd
     // internal messages (connection details, MHD state) through the
     // log_error callback. Must not be set in production builds. Guard
     // with `#ifndef NDEBUG` or an explicit environment check.
     create_webserver& debug(bool enable = true) { _config.debug = enable; return *this; }
     create_webserver& pedantic(bool enable = true) { _config.pedantic = enable; return *this; }
     /**
      * Restore the v1 behaviour of surfacing the
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
      */
     create_webserver& expose_exception_messages(bool enable = true) {
         _config.expose_exception_messages = enable; return *this;
     }

     /**
      * Restore the v1 behaviour of streaming credential
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
         _config.expose_credentials_in_logs = enable; return *this;
     }

     create_webserver& https_mem_key(const std::string& v) { _config.https_mem_key = http::load_file(v); return *this; }
     create_webserver& https_mem_cert(const std::string& v) { _config.https_mem_cert = http::load_file(v); return *this; }
     create_webserver& https_mem_trust(const std::string& v) { _config.https_mem_trust = http::load_file(v); return *this; }
     create_webserver& raw_https_mem_key(const std::string& v) { _config.https_mem_key = v; return *this; }
     create_webserver& raw_https_mem_cert(const std::string& v) { _config.https_mem_cert = v; return *this; }
     create_webserver& raw_https_mem_trust(const std::string& v) { _config.https_mem_trust = v; return *this; }
     create_webserver& https_priorities(std::string v) { _config.https_priorities = std::move(v); return *this; }
     create_webserver& cred_type(const http::http_utils::cred_type_T& v) { _config.cred_type = v; return *this; }
     create_webserver& psk_cred_handler(psk_cred_handler_callback v) { _config.psk_cred_handler = std::move(v); return *this; }
     create_webserver& digest_auth_random(std::string v) { _config.digest_auth_random = std::move(v); return *this; }
     create_webserver& nonce_nc_size(int v) { check_non_negative("nonce_nc_size", v); _config.nonce_nc_size = v; return *this; }
     create_webserver& default_policy(const http::http_utils::policy_T& v) { _config.default_policy = v; return *this; }

     // Setter is unconditional. The actual
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
     create_webserver& basic_auth(bool enable = true) { _config.basic_auth_enabled = enable; return *this; }
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
     create_webserver& digest_auth(bool enable = true) { _config.digest_auth_enabled = enable; return *this; }
     create_webserver& deferred(bool enable = true) { _config.deferred_enabled = enable; return *this; }
     create_webserver& regex_checking(bool enable = true) { _config.regex_checking = enable; return *this; }
     create_webserver& ip_access_control(bool enable = true) { _config.ip_access_control_enabled = enable; return *this; }
     create_webserver& post_process(bool enable = true) { _config.post_process_enabled = enable; return *this; }
     create_webserver& put_processed_data_to_content(bool enable = true) { _config.put_processed_data_to_content = enable; return *this; }

     create_webserver& file_upload_target(const file_upload_target_T& v) { _config.file_upload_target = v; return *this; }
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
         _config.file_upload_dir = v; return *this;
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
     create_webserver& generate_random_filename_on_upload(bool enable = true) { _config.generate_random_filename_on_upload = enable; return *this; }
     create_webserver& single_resource(bool enable = true) { _config.single_resource = enable; return *this; }
     create_webserver& tcp_nodelay(bool enable = true) { _config.tcp_nodelay = enable; return *this; }
     // The handler/callback setters and the remaining TLS / connection-
     // tuning setters live in a sibling header to keep this class
     // definition under the project per-file LOC ceiling. The inner gate
     // forces the header to be included only from within this class body.
#define SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_INSIDE_CLASS_
#include "httpserver/create_webserver_setters.hpp"
#undef SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_INSIDE_CLASS_

 private:
     // Throw helpers. Defined inline so the
     // single-line setters above stay one-liners.
     static void throw_invalid(const char* name, int64_t value, const char* range) {
         throw std::invalid_argument(std::string(name) + ": " + std::to_string(value) + " out of range " + range);
     }
     static void check_non_negative(const char* name, int v) {
         if (v < 0) throw std::invalid_argument(std::string(name) + ": " + std::to_string(v) + " must be >= 0");
     }

     // All builder inputs live in one struct (see webserver_config above).
     // Setters mutate _config; the webserver constructor copies it whole.
     webserver_config _config;

     friend class webserver;
};

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_CREATE_WEBSERVER_HPP_
