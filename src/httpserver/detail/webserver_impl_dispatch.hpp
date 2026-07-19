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

// The 404/405/500 synthesis (not_found_page / method_not_allowed_page /
// internal_error_page / run_internal_error_handler_safely) moved to the
// error_pages behavior service (DR-014 §4.11), and the log_dispatch_error
// member forwarder was removed once its last callers (the v1 alias hooks in
// webserver_aliases.cpp) began calling the detail::log_dispatch_error free
// function (dispatch_util.hpp, over the config bag) directly. No
// webserver_impl error-page or log forwarder survives.

// The lifecycle hook-firing helpers (the eleven per-phase fire_* and the
// four gated fire_*_gated helpers) moved to the hook_dispatcher behavior
// service (DR-014 §4.11); their webserver_impl forwarders were removed once
// the dispatch services and the MHD-adapter trampolines
// (connection_notify / policy_callback / request_completed) began calling
// impl_->hooks_dispatch_ directly.

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

// The final materialise/decorate/queue stage moved to the
// response_materializer behavior service (DR-014 §4.11); its webserver_impl
// forwarder was removed once request_dispatcher::finalize_answer began
// calling impl_->response_mat_ directly.

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
