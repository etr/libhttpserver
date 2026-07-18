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
// Synth-response helpers return http_response by value; the dispatch
// path moves into modded_request::response.
::httpserver::http_response not_found_page(modded_request* mr) const;
::httpserver::http_response method_not_allowed_page(modded_request* mr) const;
// Error-propagation entry point. @p msg carries the
// originating exception's text (e.what() for std::exception, the
// sentinel "unknown exception" for non-std throws, or a fixed
// internal-diagnostic string for the few non-handler-throw call
// sites that synthesise a 500 with no exception in flight).
//
// Behaviour:
//   - parent->config.internal_error_handler set, !force_our:
//       invoke it with (*mr->request, msg) and return the result.
//   - force_our=true: return a hardcoded 500 with an EMPTY body
//       (the "double-fault" fallback used when the user handler
//       itself threw).
//   - otherwise (no handler set, !force_our): return a default 500
//       whose body is the fixed string "Internal Server Error"
//       (CWE-209). @p msg is forwarded into the
//       body only when parent->config.expose_exception_messages is true
//       (development opt-in).
//
// Throws nothing on its own; if parent->config.internal_error_handler
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

// Log @p msg via parent->config.log_error if a logger is configured.
// Swallows any exception thrown by the logger -- dispatch must never
// re-enter the catch from inside its own catch. Marked noexcept because
// the outer catch(...) absorbs any bad_alloc from std::string(msg)
// construction, so no exception can escape.
//
// @p msg is forwarded to the user
// log_error callback UNCHANGED, regardless of
// create_webserver::expose_exception_messages. The error log is the
// canonical destination for verbatim exception text in v2; the HTTP
// response body is the path that was sanitized. Handlers that may
// throw exceptions containing sensitive data (DB connection strings,
// credentials, attacker-influenced input) SHOULD catch and sanitize
// the exception's what() before re-throwing if those values must not
// appear in the server log either. See also @ref
// internal_error_handler_t @security block in create_webserver.hpp.
void log_dispatch_error(std::string_view msg) const noexcept;

// Lifecycle hook firing helpers.
//
// Each helper snapshots the relevant phase vector under a shared_lock
// on hook_table_mutex_, releases the lock, then iterates the snapshot
// invoking each hook inside a try/catch. Any exception is routed
// through log_dispatch_error so the MHD callback stays clean.
// The helpers are noexcept: even an out-of-band failure (e.g.
// std::bad_alloc in the snapshot copy) is contained by std::terminate
// rather than escaping into MHD.
//
// `fire_accept_decision` returns void: the YES/NO decision is fixed
// in policy_callback BEFORE the hook fires; a throwing hook cannot
// change it.
//
// Callers MUST gate each call with the relaxed any_hooks_ short-
// circuit so zero-cost-when-unused holds.
void fire_connection_opened(
    const ::httpserver::connection_open_ctx& ctx) noexcept;
void fire_accept_decision(
    const ::httpserver::accept_ctx& ctx) noexcept;
void fire_connection_closed(
    const ::httpserver::connection_close_ctx& ctx) noexcept;

// Pre-handler short-circuit hook firing helpers.
//
// These two phases differ from the lifecycle trio in two ways:
//   1. The callable returns hook_action (not void) -- a hook can
//      respond_with(...) to short-circuit the chain. We surface the
//      short-circuit response as std::optional<http_response>: engaged
//      iff some hook short-circuited.
//   2. The context is mutable (request_received_ctx&, body_chunk_ctx&),
//      so a hook can mutate per-request state before the handler runs.
//
// Same snapshot-under-shared_lock pattern as the lifecycle trio. Same exception
// containment: a throwing hook is caught + logged via
// log_dispatch_error and treated as pass(); the chain continues. Callers
// MUST gate each call with the relaxed any_hooks_ short-circuit so
// zero-cost-when-unused holds.
[[nodiscard]] std::optional<::httpserver::http_response>
fire_request_received(::httpserver::request_received_ctx& ctx) noexcept;

[[nodiscard]] std::optional<::httpserver::http_response>
fire_body_chunk(::httpserver::body_chunk_ctx& ctx) noexcept;

// route_resolved + before_handler firing helpers.
//
// route_resolved is observation-only (void return, const ctx). It fires
// from finalize_answer right after resolve_resource_for_request returns
// — both on hit and on miss — so user-supplied hooks can observe routing
// decisions without the ability to alter the in-flight response.
//
// before_handler is short-circuit-capable. It fires from
// dispatch_resource_handler AFTER the existing post-processor destroy
// step and BEFORE the is_allowed check + resource invocation. A hook
// returning hook_action::respond_with(r) skips both the method-allowed
// check and the resource handler, and the response goes straight to
// materialization.
//
// Same exception containment as the phases above: a throwing hook is caught
// + logged via log_dispatch_error and treated as pass(). Callers MUST
// gate each call with the relaxed any_hooks_ short-circuit so
// zero-cost-when-unused holds.
void fire_route_resolved(
    const ::httpserver::route_resolved_ctx& ctx) noexcept;

[[nodiscard]] std::optional<::httpserver::http_response>
fire_before_handler(::httpserver::before_handler_ctx& ctx) noexcept;

// handler_exception firing helper.
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
// emits the hardcoded empty-body 500 DIRECTLY,
// WITHOUT re-invoking the user internal_error_handler: the alias slot
// has already had its turn at this request, so a second call would
// observably invoke the user code twice.
//
// A throwing hook in THIS phase is caught, logged via
// log_dispatch_error, and the chain CONTINUES to the next hook -- this
// is the one phase that does not abort to the standard 500 path on a
// throwing hook, because the whole point of the chain IS exception
// recovery.
// The same containment applies to the alias slot.
//
// noexcept: same contract as fire_before_handler. Snapshot-copy failure
// is logged and degraded to "as if no hooks were registered".
[[nodiscard]] std::optional<::httpserver::http_response>
fire_handler_exception(
    const ::httpserver::handler_exception_ctx& ctx) noexcept;

// after_handler / response_sent / request_completed firing
// helpers.
//
// after_handler is short-circuit-capable: a hook returning
// hook_action::respond_with(r2) REPLACES the in-flight response. Same
// thread_local snapshot pattern as the other short-circuit phases; same
// exception containment via log_dispatch_error.
//
// response_sent and request_completed are observation-only (void return,
// const ctx). response_sent has an alias-slot tail (log_access_alias_)
// invoked AFTER the user vector, exactly like the
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

// Gated-fire helpers, members so the http_response friendship
// applies (response_sent_ctx::bytes_queued reads response.body_->size()).
// Definitions live in src/detail/webserver_hook_firing.cpp.
//
// @p resource is the resolved resource borrowed from finalize_answer's
// owning shared_ptr (nullptr on the 404 / short-circuit paths where no
// resource was resolved). Passing it avoids a per-request weak_ptr
// lock() -- and its control-block atomics -- on the zero-hook hot path;
// the resource stays alive for the whole finalize_answer scope, which
// both of these fire sites run within.
void fire_after_handler_gated(modded_request* mr,
                              ::httpserver::http_resource* resource);
void fire_response_sent_gated(modded_request* mr,
                              ::httpserver::http_resource* resource);
// request_completed fires from the MHD completion callback, after
// finalize_answer's shared_ptr is gone, so it keeps the weak_ptr lock()
// -- but gated behind mr->route_has_hook_table_ so the lock is skipped
// entirely on the common zero-per-route-hook path.
void fire_request_completed_gated(modded_request* mr,
                                  enum MHD_RequestTerminationCode toe);

// Gated fire of before_handler, server-wide AND per-route,
// with short-circuit detection. Returns true iff either chain
// short-circuited (mr->response already populated; caller must go
// straight to materialize_and_queue_response). False means both chains
// passed (or both gates were closed) and dispatch should proceed.
// Definition lives in src/detail/webserver_hook_firing.cpp alongside the
// other gated-fire helpers. @p hrm is the resolved resource (non-null at
// the call site in finalize_answer).
bool fire_before_handler_gated(
    modded_request* mr,
    const std::shared_ptr<::httpserver::http_resource>& hrm);  // NOLINT(build/include_what_you_use)

// Invoke the user-supplied internal_error_handler safely.
// On success, returns the response it produced. If the user handler
// itself throws, logs generically via log_dispatch_error and returns
// a hardcoded empty-body 500.
::httpserver::http_response
    run_internal_error_handler_safely(modded_request* mr,
                                      std::string_view msg) const;
bool should_skip_auth(std::string_view path) const;

// The webserver::start() option-array + start-flag builders
// (build_mhd_option_array / add_*_mhd_options / compose_*_flags) live in
// the daemon_lifecycle collaborator (src/detail/daemon_lifecycle.cpp),
// reached through impl_->daemon_.

// The request-processing behavior moved to the DR-014 §4.11 services:
// requests_answer_first_step / requests_answer_second_step / complete_request
// -> request_pipeline (impl_->pipeline_); finalize_answer /
// resolve_resource_for_request / dispatch_resource_handler ->
// request_dispatcher (impl_->dispatcher_); the websocket-upgrade probe ->
// websocket_upgrader (impl_->ws_upgrader_). answer_to_connection (below)
// forwards into pipeline_.

// Serialize an allowed-method set into the comma-separated value
// expected by the HTTP `Allow:` header. Enum-declaration order:
// GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH.
std::string serialize_allow_methods(method_set allowed) const;  // NOLINT(build/include_what_you_use)

// Final stage of finalize_answer: build an MHD_Response from
// mr->response, decorate it, queue it on the connection. Handles
// the belt-and-suspenders fallback when get_raw_response_with_fallback
// itself fails to produce a response.
//
// @p resource is the resolved resource (nullptr when none was resolved),
// forwarded to fire_response_sent_gated so it can reach the per-route
// hook table without a weak_ptr lock(). Borrowed for the duration of the
// call from finalize_answer's owning shared_ptr.
// Forwards to the response_materializer behavior service (DR-014 §4.11),
// which owns the get_raw_response_with_fallback / queue_response_dispatching_kind
// / materialize_response / decorate_mhd_response internals.
MHD_Result materialize_and_queue_response(MHD_Connection* connection,
                                          modded_request* mr,
                                          ::httpserver::http_resource* resource);

// on_*/route registration POLICY. The v2 conflict probe + table mutation
// live in route_table (routes_.find_v2_entry_by_path_ /
// routes_.upsert_v2_table_entry_locked_); these two helpers own only the
// lambda_resource shim lifecycle (create-or-reuse + slot writes). The
// orchestrator (webserver::on_methods_) holds routes_.lock_for_write()
// across the whole prepare -> commit -> upsert sequence so the probe and
// the mutation are atomic against concurrent registrations.
//
// Returns {shim, is_fresh}: is_fresh is true when a brand-new
// lambda_resource shim was created (no entry previously existed at
// this path), false when an existing shim was reused.
std::pair<std::shared_ptr<detail::lambda_resource>, bool>  // NOLINT(build/include_what_you_use)
    prepare_or_create_lambda_shim(const detail::http_endpoint& idx,
                                  method_set methods);
void commit_handlers_to_shim(detail::lambda_resource& shim,
                             method_set methods,
                             std::function<::httpserver::http_response(
                                 const ::httpserver::http_request&)> handler);

// The multipart / file-upload handling (handle_post_form_arg,
// setup_new_upload_file_info, manage_upload_stream, process_file_upload,
// and the post_iterator file branch) lives in the upload_pipeline behavior
// service (DR-014 §4.11), reached through impl_->upload_. The post_iterator
// static MHD trampoline below stays here (its address is registered with
// MHD_create_post_processor) and forwards into that service.

// Map a wire-string HTTP method to mr->callback (pointer-to-member
// dispatch), mr->method_enum (for is_allowed checks), and mr->has_body
// (for the body-buffering branch in requests_answer_first_step).
// Unrecognised methods leave the defaults in place; finalize_answer
// then routes through the 405 path.
static void resolve_method_callback(const char* method, modded_request* mr);

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
// There is no access_log callback here: log_access is implemented as
// a response_sent hook alias (see src/detail/webserver_aliases.cpp).
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
