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

#ifndef SRC_HTTPSERVER_HTTP_REQUEST_HPP_
#define SRC_HTTPSERVER_HTTP_REQUEST_HPP_

// <microhttpd.h> and <gnutls/gnutls.h> intentionally
// do NOT appear here. Backend-coupled state lives behind
// detail/http_request_impl.hpp. The public surface no
// longer mentions any GnuTLS type: the raw session-handle accessor that
// previously returned the gnutls session is removed, replaced by
// high-level cert accessors (has_tls_session / has_client_certificate /
// get_client_cert_* / is_client_cert_verified). The lone remaining
// backend-typed name on the surface is `struct MHD_Connection*` on the
// private MHD-bound constructor (gated by HTTPSERVER_COMPILATION below).

#include <stddef.h>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <map>
#include <memory>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/cookie.hpp"
#include "httpserver/http_arg_value.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/file_info.hpp"
#include "httpserver/create_webserver.hpp"

#ifdef HTTPSERVER_COMPILATION
// Forward-declare MHD_Connection at GLOBAL scope. The internal
// MHD_Connection*-taking constructor on http_request (declared below
// inside namespace httpserver) is gated on HTTPSERVER_COMPILATION.
// Without this top-level declaration, the elaborated type specifier
// `struct MHD_Connection*` inside `namespace httpserver` would inject
// `httpserver::MHD_Connection` and shadow the real (global)
// MHD_Connection that <microhttpd.h> defines, producing a chain of
// "cannot convert MHD_Connection*" errors in src/http_request.cpp.
struct MHD_Connection;
#endif  // HTTPSERVER_COMPILATION

namespace httpserver {

namespace detail {
struct modded_request;
class webserver_impl;
class http_request_impl;
// Custom deleter for http_request_impl. Used by the
// std::unique_ptr<http_request_impl, http_request_impl_deleter> below
// so the destructor path Just Works for both heap- and arena-allocated
// impls. Definition lives out-of-line in src/http_request.cpp; this
// forward-declaration alone keeps <memory_resource> off the public
// header. The deleter holds a single function pointer (no allocator
// state spelled in the public type), so sizeof(unique_ptr<impl,
// deleter>) is two pointers regardless of where the impl is allocated.
struct http_request_impl_deleter {
    using fn_t = void (*)(http_request_impl*);
    fn_t fn = nullptr;
    void operator()(http_request_impl* p) const noexcept;
};
}  // namespace detail

/**
 * Class representing an abstraction for an Http Request. It is used from
 * classes using these APIs to receive information through the HTTP protocol.
 *
 * ### string_view lifetime contract
 *
 * Several getter methods return `std::string_view` rather than `std::string`
 * for zero-copy access to request data that lives in a per-connection arena.
 * **All `std::string_view` values returned by this class are valid for the
 * lifetime of the request object (typically the duration of the handler
 * invocation).** They alias arena-backed storage that is released by the
 * request-completion callback once the handler returns.
 *
 * Concretely: do NOT store a `std::string_view` from any getter in a
 * variable with a lifetime that outlasts the handler invocation.  If you
 * need the data beyond the handler, copy it into a `std::string`:
 *
 *     // Safe: copy before the handler returns.
 *     std::string username_copy(request.get_user());
 *
 *     // UNSAFE: the view is dangling after the handler returns.
 *     std::string_view view = request.get_user();  // captured past return!
 *
 * Getters affected: get_arg_flat(), get_args_flat(), get_querystring(),
 * get_user(), get_pass(), get_digested_user(), get_header(), get_footer(),
 * get_cookie(), get_requestor().
 * (CWE-416 Use After Free.)
 *
 * ### Container reference lifetime contract
 *
 * The container getters `get_headers()`, `get_footers()`, `get_cookies()`,
 * `get_args()`, `get_path_pieces()`, and `get_files()` all return a
 * `const ContainerType&` rather than a by-value copy. The reference and
 * any iterators / pointers / element references derived from it remain
 * valid until the `http_request` object is destroyed (typically when the
 * handler invocation returns).
 *
 * In particular, the `std::string_view` keys and values held inside the
 * `header_view_map` and `arg_view_map` returned by these getters carry the
 * same lifetime restriction as the standalone `std::string_view` accessors
 * above: do not store them past the handler call frame. Copy explicitly
 * if a longer lifetime is required.
 *
 * Implementation note: the first call to get_headers / get_footers /
 * get_cookies / get_args / get_path_pieces lazily populates a per-request
 * cache; subsequent calls are O(1) and return the same reference.
 *
 * ### Thread-safety
 *
 * `http_request` instances are **not thread-safe**. The lazy-fill booleans
 * (`args_populated`, `headers_cache_built_`, etc.) are plain (non-atomic)
 * booleans and the cache maps are unsynchronized. Accessing the same
 * `http_request` from multiple threads simultaneously is a data race
 * (CWE-362). The documented contract is that each request is processed by
 * exactly one handler thread at a time; do not share an `http_request*`
 * across threads without external synchronization.
**/
class http_request {
 public:
     static const char EMPTY[];

     // Basic auth, TLS/client-cert, and digest-auth member declarations
     // live in a sibling header to keep this class definition under the
     // project per-file LOC ceiling. The inner gate forces the header
     // to be included only from within this class body.
#define SRC_HTTPSERVER_HTTP_REQUEST_HPP_INSIDE_CLASS_
#include "httpserver/http_request_auth.hpp"
#undef SRC_HTTPSERVER_HTTP_REQUEST_HPP_INSIDE_CLASS_

     /**
      * Method used to get the path requested.
      * @return string_view spelling the request path.
      * @note The returned view aliases storage owned by this http_request and
      *       is only valid for the lifetime of the request object (typically
      *       the duration of the handler invocation). Copy into std::string
      *       to extend the lifetime.
     **/
     std::string_view get_path() const noexcept {
         return path;
     }

     /**
      * Method used to get all pieces of the path requested; considering an url splitted by '/'.
      * @return a vector of strings containing all pieces. The reference
      *         remains valid until the http_request is destroyed.
     **/
     [[nodiscard]] const std::vector<std::string>& get_path_pieces() const;

     /**
      * Method used to obtain a specified piece of the path; considering an url splitted by '/'.
      * @param index the index of the piece selected
      * @return the selected piece in form of string
     **/
     const std::string get_path_piece(int index) const;

     /**
      * Method used to get the METHOD used to make the request.
      * @return string_view spelling the request method (GET / POST / ...).
      * @note The returned view aliases storage owned by this http_request and
      *       is only valid for the lifetime of the request object (typically
      *       the duration of the handler invocation). Copy into std::string
      *       to extend the lifetime.
     **/
     std::string_view get_method() const noexcept {
         return method;
     }

     // Container and single-key data accessors live in a sibling header
     // to keep this class definition under the project per-file LOC
     // ceiling. The inner gate forces the header to be included only
     // from within this class body.
#define SRC_HTTPSERVER_HTTP_REQUEST_HPP_INSIDE_CLASS_
#include "httpserver/http_request_getters.hpp"
#undef SRC_HTTPSERVER_HTTP_REQUEST_HPP_INSIDE_CLASS_

     /**
      * Method used to get the content of the request.
      * @return string_view over the request body.
      * @note The returned view aliases storage owned by this http_request and
      *       is only valid for the lifetime of the request object (typically
      *       the duration of the handler invocation). Copy into std::string
      *       to extend the lifetime.
     **/
     std::string_view get_content() const noexcept {
         return content;
     }

     /**
      * Method to check whether the size of the content reached or exceeded content_size_limit.
      * @return boolean
     **/
     bool content_too_large() const {
         return content.size() >= content_size_limit;
     }
     /**
      * Method used to get the content of the query string.
      * @return string_view over the assembled query string (e.g. "?a=1&b=2"),
      *         empty when no query was supplied.
      * @note The returned view aliases storage owned by this http_request and
      *       is only valid for the lifetime of the request object (typically
      *       the duration of the handler invocation). Copy into std::string
      *       to extend the lifetime.
      * @note The querystring is assembled lazily on the first call and
      *       cached; the reader is `noexcept` (an allocation failure during
      *       assembly yields an empty view rather than throwing).
     **/
     std::string_view get_querystring() const noexcept;

     /**
      * Method used to get the version of the request.
      * @return string_view spelling the HTTP version (e.g. "HTTP/1.1").
      * @note The returned view aliases storage owned by this http_request and
      *       is only valid for the lifetime of the request object (typically
      *       the duration of the handler invocation). Copy into std::string
      *       to extend the lifetime.
     **/
     std::string_view get_version() const noexcept {
         return version;
     }

     /**
      * Method used to get the requestor.
      * @return the requestor (IP address string).
      * @note The returned view is only valid within the handler's call frame.
     **/
     std::string_view get_requestor() const;

     /**
      * Method used to get the requestor port used.
      * @return the requestor port
     **/
     uint16_t get_requestor_port() const;

     friend std::ostream& operator<<(std::ostream& os, const http_request& r);

     ~http_request();

 private:
     /**
      * Default constructor of the class. It is a specific responsibility of apis to initialize this type of objects.
     **/
     http_request() = default;

#ifdef HTTPSERVER_COMPILATION
     // Internal-only constructor: takes a live MHD_Connection*. Hidden
     // from public consumers via the HTTPSERVER_COMPILATION gate.
     // See the MHD_Connection forward-declaration comment at global scope
     // above for the namespace-injection rationale.
     http_request(MHD_Connection* underlying_connection, unescaper_ptr unescaper);

 public:
     // Internal-only test accessor. Returns the underlying
     // MHD_Connection* the request was built against (the same pointer
     // passed to the internal constructor above). Used exclusively by
     // test/integ/connection_state_body_residue_test.cpp to peek the
     // per-connection arena and pin the CWE-226 mitigation contract.
     // NOT part of the public ABI; only visible when the library itself
     // is being compiled (gated by HTTPSERVER_COMPILATION).
     MHD_Connection* underlying_connection_for_testing() const noexcept;

 private:
#endif  // HTTPSERVER_COMPILATION

     /**
      * Copy constructor. Deleted to make class move-only. The class is move-only for several reasons:
      *  - Internal cache structure is expensive to copy
      *  - Various string members are expensive to copy
      *  - The destructor removes transient files from disk, which must only happen once.
      *  - unique_ptr members are not copyable.
     **/
     http_request(const http_request& b) = delete;
     /**
      * Move constructor.
      * @param b http_request b to move attributes from.
     **/
     http_request(http_request&& b) noexcept = default;

     /**
      * Copy-assign. Deleted to make class move-only. The class is move-only for several reasons:
      *  - Internal cache structure is expensive to copy
      *  - Various string members are expensive to copy
      *  - The destructor removes transient files from disk, which must only happen once.
      *  - unique_ptr members are not copyable.
     **/
     http_request& operator=(const http_request& b) = delete;
     http_request& operator=(http_request&& b) = default;

     // Backend-agnostic outer state. Everything else lives behind impl_.
     std::string path;
     std::string method;
     std::string content = "";
     size_t content_size_limit = std::numeric_limits<size_t>::max();
     std::string version;

     // PIMPL: backend-coupled state (MHD_Connection*, unescaper, file
     // table, parsed-args / cookies / cert caches) lives behind this
     // pointer in src/httpserver/detail/http_request_impl.hpp. The
     // dtor is out-of-line in http_request.cpp so the unique_ptr can
     // see the complete impl type.
     // The deleter is custom because the impl can be allocated
     // either on the heap (default-resource fallback / test path) or on
     // a per-connection arena (live request path). The deleter dispatches
     // to the right reclamation strategy based on a function pointer set
     // at construction.
     std::unique_ptr<detail::http_request_impl, detail::http_request_impl_deleter> impl_;

     /**
      * Method used to set an argument value by key.
      * @param key The name identifying the argument
      * @param value The value assumed by the argument
     **/
     void set_arg(const std::string& key, const std::string& value);

     /**
      * Method used to set an argument value by key.
      * @param key The name identifying the argument
      * @param value The value assumed by the argument
      * @param size The size in number of char of the value parameter.
     **/
     void set_arg(const char* key, const char* value, size_t size);

     /**
      * Method used to set an argument value by key. If a key already exists, overwrites it.
      * @param key The name identifying the argument
      * @param value The value assumed by the argument
     **/
     void set_arg_flat(const std::string& key, const std::string& value);

     void grow_last_arg(const std::string& key, const std::string& value);

     /**
      * Method used to set the content of the request
      * @param content The content to set.
     **/
     void set_content(const std::string& content) {
         this->content = content.substr(0, content_size_limit);
     }

     /**
      * Method used to set the maximum size of the content
      * @param content_size_limit The limit on the maximum size of the content and arg's.
     **/
     void set_content_size_limit(size_t content_size_limit) {
         this->content_size_limit = content_size_limit;
     }

     /**
      * Method used to append content to the request preserving the previous inserted content
      * @param content The content to append.
      * @param size The size of the data to append.
     **/
     void grow_content(const char* content, size_t size) {
         this->content.append(content, size);
         if (this->content.size() > content_size_limit) {
             this->content.resize(content_size_limit);
         }
     }

     /**
      * Method used to set the path requested.
      * @param path The path searched by the request.
     **/
     void set_path(const std::string& path) {
         this->path = path;
     }

     /**
      * Method used to set the request METHOD
      * @param method The method to set for the request
     **/
     void set_method(const std::string& method);

     /**
      * Method used to set the request http version (ie http 1.1)
      * @param version The version to set in form of string
     **/
     void set_version(const std::string& version) {
         this->version = version;
     }

     /**
      * Method used to set all arguments of the request.
      * @param args The args key-value map to set for the request.
     **/
     void set_args(const std::map<std::string, std::string>& args);

     void set_file_cleanup_callback(file_cleanup_callback_ptr callback);

     // Set the redaction-bypass bit for diagnostic streaming.
     // Plumbed from webserver::expose_credentials_in_logs at dispatch
     // time and from create_test_request::build() for unit tests.
     void set_expose_credentials_in_logs(bool v);

     friend class webserver;
     friend class detail::webserver_impl;  // PIMPL dispatch path
     friend struct detail::modded_request;
     friend class create_test_request;    // test builder accesses impl_
};

/**
 * Stream-insert a human-readable dump of @p r into @p os for diagnostic
 * logging.
 *
 * @section redaction Redaction policy (OWASP A09:2021 / CWE-312 / CWE-532)
 * By default the following fields are emitted as the fixed token
 * `"<redacted>"` instead of their plaintext values:
 *   - The Basic-auth password (`pass:"<redacted>"`)
 *   - The `Authorization` and `Proxy-Authorization` request headers
 *     (and the same names in trailers / footers), case-insensitive
 *   - Every cookie value (cookie keys are preserved for log triage)
 *
 * The username (`user:"..."`) is NOT redacted — it follows the REMOTE_USER
 * access-log convention; it is an identifier, not a secret.
 *
 * Query-string arguments are streamed verbatim. Callers that put
 * credential material in query parameters (`?token=...`) should
 * sanitize before constructing the request URL; see the warning in
 * the class-level block of @ref http_request.
 *
 * @section opt_in Restoring the verbose v1 form
 * Call `create_webserver::expose_credentials_in_logs(true)` on the
 * builder used to construct the parent @ref webserver. This restores
 * the v1 plaintext-everywhere behaviour for every
 * @ref http_request the webserver dispatches.
 *
 * @warning `expose_credentials_in_logs(true)` is DEVELOPMENT-ONLY.
 *          When the dump is routed to a log aggregator (`log_access`,
 *          `log_error`, stdout-capturing systemd, or a centralised
 *          syslog/SIEM pipeline), enabling the flag in production
 *          exposes every Basic-auth password, every Authorization
 *          header, and every cookie/session token in plaintext to
 *          anyone with read access to the log store. Guard with
 *          `#ifndef NDEBUG` or an explicit environment check.
 *
 * @warning Query-string arguments — including any `?token=...`,
 *          `?api_key=...`, or other credential-bearing parameters —
 *          are emitted verbatim regardless of the
 *          `expose_credentials_in_logs` flag. Avoid routing credential
 *          material through query strings, or strip the offending
 *          parameters before the request reaches any logging path.
 */
std::ostream &operator<< (std::ostream &os, const http_request &r);

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_REQUEST_HPP_
