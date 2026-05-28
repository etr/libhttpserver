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

// Internal detail header. Strict gate: reachable only from libhttpserver
// translation units. Carries member-function DECLARATIONS only and is
// meant to be included from WITHIN the body of class webserver_impl in
// httpserver/detail/webserver_impl.hpp. Including it elsewhere raises
// a #error (see the inner gate below).
//
// Rationale: keeps the dispatch / start-helper / MHD-trampoline surface
// organizationally grouped while the physical class definition in
// webserver_impl.hpp stays under the project per-file LOC ceiling
// (FILE_LOC_MAX in scripts/check-file-size.sh).
// cppcheck-suppress-file unusedStructMember
#if !defined(HTTPSERVER_COMPILATION)
#error "webserver_impl_dispatch.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_DISPATCH_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_DISPATCH_HPP_

#ifndef SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_HPP_INSIDE_CLASS_
#error "webserver_impl_dispatch.hpp must be included from inside the webserver_impl class body in <httpserver/detail/webserver_impl.hpp>."
#endif

// Dispatch helpers (formerly methods on webserver). Each of these
// touches both backend state on this impl and const config on the
// owning webserver (via `parent`).
//
// TASK-036: synth-response helpers return http_response by value;
// the dispatch path moves into modded_request::response (DR-010).
::httpserver::http_response not_found_page(modded_request* mr) const;
::httpserver::http_response method_not_allowed_page(modded_request* mr) const;
// TASK-031: error-propagation entry point. @p msg carries the
// originating exception's text (e.what() for std::exception, the
// sentinel "unknown exception" for non-std throws, or a fixed
// internal-diagnostic string for the few non-handler-throw call
// sites that synthesise a 500 with no exception in flight).
//
// Behaviour matches DR-009:
//   - parent->internal_error_handler set, !force_our:
//       invoke it with (*mr->dhr, msg) and return the result.
//   - force_our=true: return a hardcoded 500 with an EMPTY body
//       (the "double-fault" fallback used when the user handler
//       itself threw).
//   - otherwise (no handler set, !force_our): return a default
//       500 whose body surfaces @p msg, so the unset-handler
//       default is informative.
//
// Throws nothing on its own; if parent->internal_error_handler
// throws, that exception propagates to the caller. Callers in the
// handler-throw path use run_internal_error_handler_safely() to
// contain that double-fault.
//
// Call sites:
//   - run_internal_error_handler_safely (double-fault arm, force_our=true)
//   - get_raw_response_with_fallback (null-materializer path)
//   - materialize_and_queue_response (belt-and-suspenders fallback,
//     force_our=true, after get_raw_response_with_fallback returned null)
//   - dispatch_resource_handler (null-sentinel response from handler)
::httpserver::http_response internal_error_page(
    modded_request* mr,
    std::string_view msg,
    bool force_our = false) const;

// TASK-031: log @p msg via parent->log_error if a logger is configured.
// Swallows any exception thrown by the logger -- dispatch must never
// re-enter the catch from inside its own catch.
void log_dispatch_error(std::string_view msg) const;

// TASK-046 -- Lifecycle hook firing helpers.
//
// Each helper snapshots the relevant phase vector under a shared_lock
// on hook_table_mutex_, releases the lock, then iterates the snapshot
// invoking each hook inside a try/catch. Any exception is routed
// through log_dispatch_error so the MHD callback stays clean (DR-009
// §5.2). The helpers are noexcept: even an out-of-band failure (e.g.
// std::bad_alloc in the snapshot copy) is contained by std::terminate
// rather than escaping into MHD.
//
// `fire_accept_decision` returns void: the YES/NO decision is fixed
// in policy_callback BEFORE the hook fires (DR-012 §4.10); a throwing
// hook cannot change it.
//
// Callers MUST gate each call with the relaxed any_hooks_ short-
// circuit so zero-cost-when-unused holds (PRD-HOOK-REQ-008).
void fire_connection_opened(
    const ::httpserver::connection_open_ctx& ctx) noexcept;
void fire_accept_decision(
    const ::httpserver::accept_ctx& ctx) noexcept;
void fire_connection_closed(
    const ::httpserver::connection_close_ctx& ctx) noexcept;

// TASK-047 -- Pre-handler short-circuit hook firing helpers.
//
// These two phases differ from the TASK-046 trio in two ways:
//   1. The callable returns hook_action (not void) -- a hook can
//      respond_with(...) to short-circuit the chain. We surface the
//      short-circuit response as std::optional<http_response>: engaged
//      iff some hook short-circuited.
//   2. The context is mutable (request_received_ctx&, body_chunk_ctx&),
//      so a hook can mutate per-request state before the handler runs.
//
// Same snapshot-under-shared_lock pattern as TASK-046. Same exception
// containment: a throwing hook is caught + logged via
// log_dispatch_error and treated as pass(); the chain continues. Callers
// MUST gate each call with the relaxed any_hooks_ short-circuit so
// zero-cost-when-unused holds.
[[nodiscard]] std::optional<::httpserver::http_response>
fire_request_received(::httpserver::request_received_ctx& ctx) noexcept;

[[nodiscard]] std::optional<::httpserver::http_response>
fire_body_chunk(::httpserver::body_chunk_ctx& ctx) noexcept;

// TASK-048 -- route_resolved + before_handler firing helpers.
//
// route_resolved is observation-only (void return, const ctx). It fires
// from finalize_answer right after resolve_resource_for_request returns
// — both on hit and on miss — so user-supplied hooks can observe routing
// decisions without the ability to alter the in-flight response (DR-012).
//
// before_handler is short-circuit-capable. It fires from
// dispatch_resource_handler AFTER the existing post-processor destroy
// step and BEFORE the is_allowed check + resource invocation. A hook
// returning hook_action::respond_with(r) skips both the method-allowed
// check and the resource handler, and the response goes straight to
// materialization.
//
// Same exception containment as TASK-046/047: a throwing hook is caught
// + logged via log_dispatch_error and treated as pass(). Callers MUST
// gate each call with the relaxed any_hooks_ short-circuit so
// zero-cost-when-unused holds.
void fire_route_resolved(
    const ::httpserver::route_resolved_ctx& ctx) noexcept;

[[nodiscard]] std::optional<::httpserver::http_response>
fire_before_handler(::httpserver::before_handler_ctx& ctx) noexcept;

// TASK-049 -- handler_exception firing helper.
//
// Returns engaged optional iff some hook (user-added or the
// internal_error_handler alias slot) short-circuited with respond_with().
// The caller -- the catch arms in dispatch_resource_handler -- stashes
// that response into mr->response and falls through to
// materialize_and_queue_response in finalize_answer.
//
// Chain order:
//   1. User-added hooks in hooks_handler_exception_ (registration order).
//   2. handler_exception_alias_ (the v1 internal_error_handler), if set.
// If no hook short-circuits, returns std::nullopt -- the caller then
// emits the hardcoded empty-body 500 (DR-009 §5.2 point 4) DIRECTLY,
// WITHOUT re-invoking the user internal_error_handler: the alias slot
// has already had its turn at this request, so a second call would
// observably invoke the user code twice.
//
// Per DR-012: a throwing hook in THIS phase is caught, logged via
// log_dispatch_error, and the chain CONTINUES to the next hook -- this
// is the one phase that does not abort to DR-009 §5.2 on a throwing
// hook, because the whole point of the chain IS exception recovery.
// The same containment applies to the alias slot.
//
// noexcept: same contract as fire_before_handler. Snapshot-copy failure
// is logged and degraded to "as if no hooks were registered".
[[nodiscard]] std::optional<::httpserver::http_response>
fire_handler_exception(
    const ::httpserver::handler_exception_ctx& ctx) noexcept;

// TASK-050 -- after_handler / response_sent / request_completed firing
// helpers.
//
// after_handler is short-circuit-capable: a hook returning
// hook_action::respond_with(r2) REPLACES the in-flight response. Same
// thread_local snapshot pattern as the other short-circuit phases; same
// exception containment via log_dispatch_error.
//
// response_sent and request_completed are observation-only (void return,
// const ctx). response_sent has an alias-slot tail (log_access_alias_)
// invoked AFTER the user vector, exactly like the TASK-049
// handler_exception alias.
//
// All three are noexcept. Snapshot-copy failure is logged and degraded
// to "as if no hooks were registered".
[[nodiscard]] std::optional<::httpserver::http_response>
fire_after_handler(::httpserver::after_handler_ctx& ctx) noexcept;

void fire_response_sent(
    const ::httpserver::response_sent_ctx& ctx) noexcept;

void fire_request_completed(
    const ::httpserver::request_completed_ctx& ctx) noexcept;

// TASK-050: gated-fire helpers, members so the http_response friendship
// applies (response_sent_ctx::bytes_queued reads response.body_->size()).
// Definitions live in src/detail/webserver_finalize.cpp.
void fire_after_handler_gated(modded_request* mr);
void fire_response_sent_gated(modded_request* mr);
void fire_request_completed_gated(modded_request* mr,
                                  enum MHD_RequestTerminationCode toe);

// TASK-051: gated fire of before_handler, server-wide AND per-route,
// with short-circuit detection. Returns true iff either chain
// short-circuited (mr->response already populated; caller must go
// straight to materialize_and_queue_response). False means both chains
// passed (or both gates were closed) and dispatch should proceed.
// Definition lives in src/detail/webserver_finalize.cpp alongside the
// other gated-fire helpers. @p hrm is the resolved resource (non-null at
// the call site in finalize_answer).
bool fire_before_handler_gated(
    modded_request* mr,
    const std::shared_ptr<::httpserver::http_resource>& hrm);

// TASK-031: invoke the user-supplied internal_error_handler safely.
// On success, returns the response it produced. If the user handler
// itself throws, logs generically via log_dispatch_error and returns
// a hardcoded empty-body 500.
::httpserver::http_response
    run_internal_error_handler_safely(modded_request* mr,
                                      std::string_view msg) const;
bool should_skip_auth(const std::string& path) const;
void invalidate_route_cache();

// Helpers for webserver::start(). Each appends a logical subset of
// libmicrohttpd's option array, or composes a logical subset of the
// daemon start-flag bitmask, reading the const config bag from
// `parent`. Split for readability and to keep each function under
// the project's cyclomatic-complexity bar.
void build_mhd_option_array(std::vector<MHD_OptionItem>& iov) const;
void add_base_mhd_options(std::vector<MHD_OptionItem>& iov) const;
void add_tls_mhd_options(std::vector<MHD_OptionItem>& iov) const;
void add_gnutls_mhd_options(std::vector<MHD_OptionItem>& iov) const;
void add_extended_mhd_options(std::vector<MHD_OptionItem>& iov) const;
void add_https_extra_options(std::vector<MHD_OptionItem>& iov) const;
int compose_start_flags() const;
int compose_transport_flags() const;
int compose_runtime_flags() const;

MHD_Result requests_answer_first_step(MHD_Connection* connection, modded_request* mr);
MHD_Result requests_answer_second_step(MHD_Connection* connection,
        const char* method, const char* version, const char* upload_data,
        size_t* upload_data_size, modded_request* mr);
// TASK-021: the wire-string `method` parameter was dropped because
// finalize_answer now consults mr->method_enum (set once by
// answer_to_connection) for the is_allowed check.
MHD_Result finalize_answer(MHD_Connection* connection, modded_request* mr);

// Sub-helpers carved out of finalize_answer to keep each function
// under the cyclomatic-complexity bar. The orchestrator delegates,
// in order, to the websocket-upgrade probe, the resource lookup,
// the auth short-circuit, the dispatch path, and the response
// materialiser.
std::optional<MHD_Result> try_handle_websocket_upgrade(MHD_Connection* connection,
                                                       modded_request* mr);
#ifdef HAVE_WEBSOCKET
// Returns the validated Sec-WebSocket-Key on a well-formed RFC 6455
// handshake; nullopt if any required header is missing or malformed.
std::optional<const char*> validate_websocket_handshake(MHD_Connection* connection);

// Finish the upgrade once the handshake is validated. Returns the
// MHD_queue_response result if the upgrade was queued; nullopt if
// no handler is registered at this URL or the upgrade response
// could not be created (caller falls through to normal dispatch).
std::optional<MHD_Result> complete_websocket_upgrade(MHD_Connection* connection,
                                                     modded_request* mr,
                                                     const char* ws_key);
#endif  // HAVE_WEBSOCKET

// Carry the data the regex+cache lookup path produces back to the
// caller. matched_endpoint is needed both to extract URL parameters
// and to store the entry in the LRU cache.
struct regex_route_lookup {
    std::shared_ptr<::httpserver::http_resource> hrm;
    std::vector<std::string> url_pars;
    std::vector<int> chunks;
};

// Locate the resource serving @p mr. Returns true and sets @p hrm
// on hit (also populates URL parameters on @p mr for regex-route
// hits); false otherwise. Takes a shared lock on
// registered_resources_mutex internally.
bool resolve_resource_for_request(modded_request* mr,
        std::shared_ptr<::httpserver::http_resource>& hrm);

// LRU cache hit path: returns the cached match (hrm + url_pars +
// chunks) and promotes the entry to the front of the list. Caller
// must hold registered_resources_mutex (shared).
std::optional<regex_route_lookup>
    lookup_route_cache(const std::string& key);

// Linear regex scan with longest-match-wins tie-breaking. Returns
// the matched endpoint + resource on hit. Caller must hold
// registered_resources_mutex (shared).
struct regex_route_scan_hit {
    detail::http_endpoint endpoint;
    std::shared_ptr<::httpserver::http_resource> hrm;
};
std::optional<regex_route_scan_hit>
    scan_regex_routes(const detail::http_endpoint& target);

// Insert a (key -> matched_endpoint, hrm) entry at the front of
// the LRU and evict the oldest entry if the cache is full. Caller
// must hold registered_resources_mutex (shared).
void store_route_cache(const std::string& key,
                       const detail::http_endpoint& matched,
                       std::shared_ptr<::httpserver::http_resource> hrm);

// Walk url_pars/chunks parallel arrays and set each named parameter
// on the request, guarding against an out-of-range chunk index.
void apply_extracted_params(modded_request* mr,
        const detail::http_endpoint& target,
        const std::vector<std::string>& url_pars,
        const std::vector<int>& chunks);

// Invoke the resource handler bound to @p mr, populating
// mr->response. On is_allowed=false, queues a 405 with an Allow
// header. On handler-throw, routes through the safe internal-error
// path (TASK-031 / DR-009).
void dispatch_resource_handler(modded_request* mr,
        const std::shared_ptr<::httpserver::http_resource>& hrm);

// Serialize an allowed-method set into the comma-separated value
// expected by the HTTP `Allow:` header. Enum-declaration order
// (TASK-021): GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE,
// PATCH.
std::string serialize_allow_methods(method_set allowed) const;

// Final stage of finalize_answer: build an MHD_Response from
// mr->response, decorate it, queue it on the connection. Handles
// the belt-and-suspenders fallback when get_raw_response_with_fallback
// itself fails to produce a response.
MHD_Result materialize_and_queue_response(MHD_Connection* connection,
                                          modded_request* mr);

// Helpers carved out of webserver::on_methods_ to stay under the
// cyclomatic-complexity bar. The orchestrator on_methods_ retains
// input validation and the public ordering; everything that
// mutates the v1 maps or the v2 3-tier table lives here.
//
// Caller must hold registered_resources_mutex (unique_lock) for
// prepare_or_create_lambda_shim, commit_handlers_to_shim, and
// insert_fresh_v1_entries. upsert_v2_table_entry takes
// route_table_mutex_ internally.
std::shared_ptr<detail::lambda_resource>
    prepare_or_create_lambda_shim(const detail::http_endpoint& idx,
                                  method_set methods,
                                  bool& fresh_out);
void commit_handlers_to_shim(detail::lambda_resource& shim,
                             method_set methods,
                             const std::function<::httpserver::http_response(
                                 const ::httpserver::http_request&)>& handler);
void insert_fresh_v1_entries(const detail::http_endpoint& idx,
                             std::shared_ptr<::httpserver::http_resource> shim);
void upsert_v2_table_entry(const detail::http_endpoint& idx,
                           method_set methods,
                           std::shared_ptr<::httpserver::http_resource> shim,
                           bool fresh);
void upsert_v2_radix_route(const std::string& key,
                           method_set methods,
                           std::shared_ptr<::httpserver::http_resource> shim);
void insert_fresh_v2_entry(const detail::http_endpoint& idx,
                           method_set methods,
                           std::shared_ptr<::httpserver::http_resource> shim);
void update_existing_v2_entry(const std::string& key,
                              method_set methods,
                              std::shared_ptr<::httpserver::http_resource> shim);

// Helpers carved out of post_iterator. The MHD post-iterator
// trampoline is a static MHD callback; the orchestrator below
// (process_file_upload) is an instance method because it reads the
// const config bag on parent (file_upload_target,
// generate_random_filename_on_upload, file_upload_dir).
static MHD_Result handle_post_form_arg(modded_request* mr,
                                       const char* key,
                                       const char* data,
                                       size_t size,
                                       uint64_t off);
bool setup_new_upload_file_info(::httpserver::http::file_info& file,
                                const char* filename,
                                const char* content_type,
                                const char* transfer_encoding) const;
static void manage_upload_stream(modded_request* mr,
                                 const char* filename,
                                 const char* key,
                                 ::httpserver::http::file_info& file);
MHD_Result process_file_upload(modded_request* mr,
                               const char* key,
                               const char* filename,
                               const char* content_type,
                               const char* transfer_encoding,
                               const char* data,
                               size_t size) const;

// Mirror a register_path / register_prefix call into the v2 3-tier
// route table. Distinct from upsert_v2_table_entry (which is the
// on_*/route path with methods merging): this is a one-shot insert
// with method_set::set_all() and no merge. Takes route_table_mutex_
// internally.
void register_v2_route(const detail::http_endpoint& idx,
                       std::shared_ptr<::httpserver::http_resource> res,
                       bool family);

// Map a wire-string HTTP method to mr->callback (pointer-to-member
// dispatch), mr->method_enum (for is_allowed checks), and mr->has_body
// (for the body-buffering branch in requests_answer_first_step).
// Unrecognised methods leave the defaults in place; finalize_answer
// then routes through the 405 path.
static void resolve_method_callback(const char* method, modded_request* mr);

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
// Per-connection lifetime callback. cls is unused (nullptr).
// socket_context is MHD's per-connection void* slot: we new/delete a
// detail::connection_state through it on STARTED/CLOSED, so the
// arena lives exactly as long as the MHD_Connection does.
static void connection_notify(void* cls, struct MHD_Connection* connection,
                              void** socket_context,
                              enum MHD_ConnectionNotificationCode toe);
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
// TASK-050: webserver_impl::access_log removed. log_access is now a
// response_sent hook alias (see src/detail/webserver_aliases.cpp).
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

#endif  // SRC_HTTPSERVER_DETAIL_WEBSERVER_IMPL_DISPATCH_HPP_
